/**********************************************************************

  jit.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "gc.h"
#include "vm_core.h"
#include "internal.h"
#include "insns.inc"
#include "insns_info.inc"
#include "vm_insnhelper.h"
#include "vm_exec.h"
#include "iseq.h"

#define JIT_HOST 1
#include "jit.h"
#include "ruby_jit.h"
#include "jit_config.h"
#include "jit_prelude.c"
#include "jit_hashmap.c"
// static const char cmd_template[];
#include "jit_cgen_cmd.h"
#include "lir_template.h"
#include "jit_ruby_api.c"

#include <dlfcn.h> // dlopen, dlclose, dlsym
#include <sys/time.h> // gettimeofday

#define LOG(MSG, ...)                        \
    if (JIT_DEBUG_VERBOSE) {                 \
	fprintf(stderr, MSG, ##__VA_ARGS__); \
	fprintf(stderr, "\n");               \
    }
#define RC_INC(O) ((O)->refc++)
#define RC_DEC(O) (--(O)->refc)
#define RC_INIT(O) ((O)->refc = 1)
#define RC_CHECK0(O) ((O)->refc == 0)

#undef REG_CFP
#undef REG_PC
#define REG_CFP ((e)->cfp)
#define REG_PC ((e)->pc)

// rujit
typedef struct rb_jit_t rb_jit_t;

typedef struct jit_trace trace_t;

struct memory_pool {
    struct page_chunk *head;
    struct page_chunk *root;
    unsigned pos;
    unsigned size;
};

typedef struct bloom_filter {
    uintptr_t bits;
} bloom_filter_t;

typedef struct jit_list {
    uintptr_t *list;
    unsigned size;
    unsigned capacity;
} jit_list_t;

typedef struct const_pool {
    jit_list_t list;
    jit_list_t inst;
} const_pool_t;

enum regstack_state {
    REGSTACK_DEFAULT,
    REGSTACK_COMPILED,
    REGSTACK_ERROR = -1
};

typedef struct regstack {
    jit_list_t list;
    enum regstack_state status;
    trace_exit_status_t flag;
    long refc;
    VALUE *pc;
} regstack_t;

typedef struct lir_inst_t {
    unsigned id;
    unsigned short flag;
    unsigned short opcode;
    struct lir_basicblock_t *parent;
    jit_list_t *user;
    VALUE *addr;
} lir_inst_t, *lir_t;

#define LIR_INST_VARIANT ((unsigned short)(1 << 0))

typedef struct variable_table variable_table_t;

typedef struct lir_basicblock_t {
    lir_inst_t base;
    VALUE *start_pc;
    variable_table_t *init_table;
    variable_table_t *last_table;
    jit_list_t insts;
    jit_list_t succs;
    jit_list_t preds;
    jit_list_t stack_map;
} basicblock_t;
#define GET_STACK_MAP_ENTRY_SIZE(MAP) ((MAP)->size / 2)
#define GET_STACK_MAP_REAL_INDEX(IDX) ((IDX)*2)

typedef trace_side_exit_handler_t *(*native_func_t)(rb_thread_t *, rb_control_frame_t *);

struct jit_trace {
    native_func_t code;
    VALUE *start_pc;
    VALUE *last_pc;
    trace_side_exit_handler_t *parent;
    // for debug usage
    const rb_iseq_t *iseq;
    long counter;
    long failed_counter;
    long refc;
    void *handler;
    hashmap_t *side_exit;
    const_pool_t cpool;
#if JIT_DEBUG_TRACE
    char *func_name;
#endif
#ifdef ENABLE_PROFILE_TRACE_JIT
    long invoked;
#endif
};

#define TRACE_ERROR_INFO(OP, TAIL)                                        \
    OP(OK, "ok")                                                          \
    OP(NATIVE_METHOD, "invoking native method")                           \
    OP(THROW, "throw exception")                                          \
    OP(UNSUPPORT_OP, "not supported bytecode")                            \
    OP(LEAVE, "this trace return into native method")                     \
    OP(REGSTACK_UNDERFLOW, "register stack underflow")                    \
    OP(ALREADY_RECORDED, "this instruction is already recorded on trace") \
    OP(BUFFER_FULL, "trace buffer is full")                               \
    TAIL

#define DEFINE_TRACE_ERROR_STATE(NAME, MSG) TRACE_ERROR_##NAME,
#define DEFINE_TRACE_ERROR_MESSAGE(NAME, MSG) MSG,
enum trace_error_state {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_STATE, TRACE_ERROR_END = -1)
};

static const char *trace_error_message[] = {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_MESSAGE, "")
};

typedef struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    trace_t *trace;
    int opcode;
    enum trace_error_state reason;
} jit_event_t;

typedef struct trace_recorder {
    jit_event_t *current_event;
    trace_t *trace;
    basicblock_t *cur_bb;
    basicblock_t *entry_bb;
    struct lir_inst_t **insts;
    unsigned flag;
    unsigned last_inst_id;
    struct memory_pool mpool;
    jit_list_t bblist;
    jit_list_t cache_pool;
    unsigned cache_max;
    regstack_t regstack;
    int stack_offset;
    int stack_depth;
} trace_recorder_t;

typedef enum trace_mode {
    TraceModeDefault = 0,
    TraceModeRecording = 1,
    TraceModeError = -1
} trace_mode_t;

struct rb_jit_t {
    VALUE self;
    trace_t *current_trace;
    trace_recorder_t *recorder;
    trace_mode_t mode;
    rb_thread_t *main_thread;
    hashmap_t traces;
    hashmap_t blacklist;
    hashmap_t block_contained;
    bloom_filter_t filter;
    bloom_filter_t blacklist_filter;
    jit_list_t trace_list;
    jit_list_t method_cache;
};

static int disable_jit = 0;
jit_runtime_t jit_runtime = {};
/* global variables { */
static rb_jit_t *current_jit = NULL;
int rujit_record_trace_mode = 0;
static VALUE rb_cMath;
#define JIT_PROFILE_ENTER(msg) jit_profile((msg), 0)
#define JIT_PROFILE_LEAVE(msg, cond) jit_profile((msg), (cond))
#ifdef ENABLE_PROFILE_TRACE_JIT
static uint64_t time_trace_search = 0;
static uint64_t invoke_trace_invoke_enter = 0;
static uint64_t invoke_trace_invoke = 0;
static uint64_t invoke_trace_child1 = 0;
static uint64_t invoke_trace_child2 = 0;
static uint64_t invoke_trace_exit = 0;
static uint64_t invoke_trace_side_exit = 0;
static uint64_t invoke_trace_success = 0;
static uint64_t invoke_bloomfilter_hit = 0;
static uint64_t invoke_bloomfilter_total = 0;
static uint64_t invoke_bloomfilter_entry = 0;
#define JIT_PROFILE_COUNT(COUNTER) ((COUNTER) += 1)
#else
#define JIT_PROFILE_COUNT(COUNTER)
#endif

/* } global variables */

static uint64_t jit_profile(const char *msg, int print_log)
{
    static uint64_t last = 0;
    uint64_t time, diff;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    diff = time - last;
    if (print_log) {
	fprintf(stderr, "%s : %u msec\n", msg, (unsigned)diff);
    }
    last = time;
    return diff;
}

static jit_event_t *jit_event_init(jit_event_t *e, rb_jit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
{
    ISEQ iseq = cfp->iseq;
    VALUE *iseq_orig = rb_iseq_original_iseq(iseq);
    long offset = pc - iseq->iseq_encoded;
    int opcode = (int)iseq_orig[offset];
    e->th = th;
    e->cfp = cfp;
    e->pc = pc;
    e->trace = jit->current_trace;
    e->opcode = opcode;
    jit->recorder->current_event = e;
    return e;
}

static lir_t trace_recorder_insert_stackpop(trace_recorder_t *rec);
static void mark_trace_header(trace_side_exit_handler_t *handler, rb_thread_t *th);
static void dump_trace(trace_recorder_t *rec);
static void stop_recording(rb_jit_t *jit);
static lir_t Emit_StackPop(trace_recorder_t *rec);
static lir_t Emit_Jump(trace_recorder_t *rec, basicblock_t *bb);
static lir_t Emit_Exit(trace_recorder_t *rec, VALUE *target_pc);
static int lir_is_terminator(lir_inst_t *inst);
static int lir_is_guard(lir_inst_t *inst);
static int elimnate_guard(trace_recorder_t *rec, lir_inst_t *inst);
static void trace_optimize(trace_recorder_t *rec, trace_t *trace);
static void trace_compile(trace_recorder_t *rec, trace_t *trace);
static void trace_link(trace_side_exit_handler_t *parent, trace_t *child);

/* redefined_flag { */
static short jit_vm_redefined_flag[JIT_BOP_EXT_LAST_];
static st_table *jit_opt_method_table = 0;

#define MATH_REDEFINED_OP_FLAG (1 << 9)
static int
jit_redefinition_check_flag(VALUE klass)
{
    if (klass == rb_cFixnum)
	return FIXNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cFloat)
	return FLOAT_REDEFINED_OP_FLAG;
    if (klass == rb_cString)
	return STRING_REDEFINED_OP_FLAG;
    if (klass == rb_cArray)
	return ARRAY_REDEFINED_OP_FLAG;
    if (klass == rb_cHash)
	return HASH_REDEFINED_OP_FLAG;
    if (klass == rb_cBignum)
	return BIGNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cSymbol)
	return SYMBOL_REDEFINED_OP_FLAG;
    if (klass == rb_cTime)
	return TIME_REDEFINED_OP_FLAG;
    if (klass == rb_cRegexp)
	return REGEXP_REDEFINED_OP_FLAG;
    if (klass == rb_cMath)
	return MATH_REDEFINED_OP_FLAG;
    return 0;
}

void rb_jit_check_redefinition_opt_method(const rb_method_entry_t *me, VALUE klass)
{
#define ruby_vm_redefined_flag GET_VM()->redefined_flag
    st_data_t bop;
    int flag;
    if (UNLIKELY(disable_jit)) {
	return;
    }
    flag = jit_redefinition_check_flag(klass);
    if (st_lookup(jit_opt_method_table, (st_data_t)me, &bop)) {
	assert(flag != 0);
	if ((jit_vm_redefined_flag[bop] & ruby_vm_redefined_flag[bop]) == ruby_vm_redefined_flag[bop]) {
	    jit_vm_redefined_flag[bop] |= ruby_vm_redefined_flag[bop];
	}
	jit_vm_redefined_flag[bop] |= flag;
    }
}

/* copied from vm.c */
static void add_opt_method(VALUE klass, ID mid, VALUE bop)
{
    rb_method_entry_t *me = rb_method_entry_at(klass, mid);

    if (me && me->def && me->def->type == VM_METHOD_TYPE_CFUNC) {
	st_insert(jit_opt_method_table, (st_data_t)me, (st_data_t)bop);
    }
    else {
	rb_bug("undefined optimized method: %s", rb_id2name(mid));
    }
}

static void rb_jit_init_redefined_flag(void)
{
#define DEF(k, mid, bop)                              \
    do {                                              \
	jit_vm_redefined_flag[bop] = 0;               \
	add_opt_method(rb_c##k, rb_intern(mid), bop); \
    } while (0)
    jit_opt_method_table = st_init_numtable();
    DEF(Fixnum, "&", JIT_BOP_AND);
    DEF(Fixnum, "|", JIT_BOP_OR);
    DEF(Fixnum, "^", JIT_BOP_XOR);
    DEF(Fixnum, ">>", JIT_BOP_RSHIFT);
    DEF(Fixnum, "~", JIT_BOP_INV);
    DEF(Fixnum, "**", JIT_BOP_POW);
    DEF(Float, "**", JIT_BOP_POW);

    DEF(Fixnum, "-@", JIT_BOP_NEG);
    DEF(Float, "-@", JIT_BOP_NEG);
    DEF(Fixnum, "to_f", JIT_BOP_TO_F);
    DEF(Float, "to_f", JIT_BOP_TO_F);
    DEF(String, "to_f", JIT_BOP_TO_F);

    DEF(Float, "to_i", JIT_BOP_TO_I);
    DEF(String, "to_i", JIT_BOP_TO_I);

    DEF(Fixnum, "to_s", JIT_BOP_TO_S);
    DEF(Float, "to_s", JIT_BOP_TO_S);
    DEF(String, "to_s", JIT_BOP_TO_S);

    DEF(Math, "sin", JIT_BOP_SIN);
    DEF(Math, "cos", JIT_BOP_COS);
    DEF(Math, "tan", JIT_BOP_TAN);
    DEF(Math, "exp", JIT_BOP_EXP);
    DEF(Math, "sqrt", JIT_BOP_SQRT);
    DEF(Math, "log10", JIT_BOP_LOG10);
    DEF(Math, "log2", JIT_BOP_LOG2);
#undef DEF
}
/* } redefined_flag */

#include "jit_core.c"

/* trace_recorder { */
static basicblock_t *trace_recorder_create_block(trace_recorder_t *rec, VALUE *pc);

static trace_recorder_t *trace_recorder_new()
{
    trace_recorder_t *recorder = (trace_recorder_t *)malloc(sizeof(*recorder));
    memset(recorder, 0, sizeof(*recorder));
    memory_pool_init(&recorder->mpool);
    jit_list_init(&recorder->bblist);
    jit_list_init(&recorder->cache_pool);
    regstack_init(&recorder->regstack, NULL);
    return recorder;
}

static void trace_recorder_delete(trace_recorder_t *recorder)
{
    jit_list_delete(&recorder->bblist);
    regstack_delete(&recorder->regstack);
    memory_pool_reset(&recorder->mpool, 0);
    free(recorder);
}

static void trace_recorder_disable_cache(trace_recorder_t *recorder);

static void trace_recorder_clear(trace_recorder_t *rec, trace_t *trace, int alloc_memory)
{
    unsigned i;
    for (i = 0; i < rec->bblist.size; i++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	basicblock_delete(bb);
    }
    rec->cur_bb = NULL;
    rec->bblist.size = 0;
    rec->last_inst_id = 0;
    rec->trace = trace;
    rec->cache_max = 0;
    trace_recorder_disable_cache(rec);
    regstack_delete(&rec->regstack);
    regstack_init(&rec->regstack, NULL);
    memory_pool_reset(&rec->mpool, alloc_memory);
    rec->entry_bb = rec->cur_bb = trace_recorder_create_block(rec, NULL);
    rec->stack_offset = 0;
}

static int trace_recorder_inst_size(trace_recorder_t *recorder)
{
    return recorder->last_inst_id;
}

static int trace_recorder_is_full(trace_recorder_t *recorder)
{
    /*reserve one instruction for EXIT */
    return trace_recorder_inst_size(recorder) - 2 >= LIR_MAX_TRACE_LENGTH;
}

static lir_t trace_recorder_get_const(trace_recorder_t *recorder, VALUE value)
{
    int idx = const_pool_contain(&recorder->trace->cpool, (const void *)value);
    if (idx >= 0) {
	return (lir_t)jit_list_get(&recorder->trace->cpool.inst, idx);
    }
    return NULL;
}

static lir_t trace_recorder_add_const(trace_recorder_t *recorder, VALUE value, lir_t reg)
{
    int idx = const_pool_add(&recorder->trace->cpool, (const void *)value, reg);
    return (lir_t)jit_list_get(&recorder->trace->cpool.inst, idx);
}

static CALL_INFO trace_recorder_alloc_cache(trace_recorder_t *recorder)
{
    CALL_INFO newci = (CALL_INFO)malloc(sizeof(*newci));
    memset(newci, 0, sizeof(rb_call_info_t));
    jit_list_add(&recorder->cache_pool, (uintptr_t)newci);
    return newci;
}

static CALL_INFO trace_recorder_clone_cache(trace_recorder_t *recorder, CALL_INFO ci)
{
    CALL_INFO newci = trace_recorder_alloc_cache(recorder);
    memcpy(newci, ci, sizeof(*newci));
    return newci;
}

static void trace_recorder_freeze_cache(trace_recorder_t *recorder)
{
    unsigned i;
    recorder->cache_max = recorder->cache_pool.size;
    for (i = 0; i < recorder->cache_max; i++) {
	CALL_INFO ci = (CALL_INFO)jit_list_get(&recorder->cache_pool, i);
	jit_list_add(&current_jit->method_cache, (uintptr_t)ci);
    }
    recorder->cache_pool.size = 0;
}

static void trace_recorder_disable_cache(trace_recorder_t *recorder)
{
    unsigned i;
    for (i = recorder->cache_max; i < recorder->cache_pool.size; i++) {
	CALL_INFO ci = (CALL_INFO)jit_list_get(&recorder->cache_pool, i);
	free(ci);
    }
    recorder->cache_pool.size = recorder->cache_max;
}

static regstack_t *trace_recorder_take_snapshot(trace_recorder_t *rec, VALUE *pc, int force_exit)
{
    basicblock_t *bb = rec->cur_bb;
    regstack_t *stack = NULL;
    int idx = jit_list_indexof(&bb->stack_map, (uintptr_t)pc);
    if (idx >= 0) {
	stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	if (regstack_equal(stack, &rec->regstack)) {
	    if (JIT_DEBUG_VERBOSE > 2) {
		fprintf(stderr, "snapshot is already copied\n");
	    }
	    return stack;
	}
	if (JIT_DEBUG_VERBOSE > 2) {
	    fprintf(stderr, "rewrite snapshot old=%p, old_pc=%p, pc=%p\n",
	            stack, stack->pc, pc);
	    regstack_dump(stack);
	    regstack_dump(&rec->regstack);
	    assert(stack->list.size == rec->regstack.list.size);
	}
	regstack_delete(stack);
    }
    stack = regstack_clone(&rec->mpool, &rec->regstack, pc);
    if (JIT_DEBUG_VERBOSE > 2) {
	// fprintf(stderr, "take snapshot pc=%p\n", pc);
	// regstack_dump(stack);
    }

    if (force_exit) {
	stack->flag = TRACE_EXIT_SUCCESS;
    }
    jit_list_add(&bb->stack_map, (uintptr_t)pc);
    jit_list_add(&bb->stack_map, (uintptr_t)stack);
    return stack;
}

static unsigned trace_recorder_get_side_exit(trace_recorder_t *rec, VALUE *pc, regstack_t **map)
{
    unsigned i;
    unsigned index = 0;
    basicblock_t *bb = rec->cur_bb;
    for (i = 0; i < rec->bblist.size; i++) {
	basicblock_t *block = (basicblock_t *)jit_list_get(&rec->bblist, i);
	if (block == bb) {
	    break;
	}
	index += GET_STACK_MAP_ENTRY_SIZE(&block->stack_map);
    }
    for (i = 0; i < GET_STACK_MAP_ENTRY_SIZE(&bb->stack_map); i++) {
	unsigned idx = GET_STACK_MAP_REAL_INDEX(i);
	VALUE *pc2 = (VALUE *)jit_list_get(&bb->stack_map, idx);
	if (pc == pc2) {
	    *map = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	    break;
	}
	index++;
    }
    return index;
}

static basicblock_t *trace_recorder_get_block(trace_recorder_t *rec, VALUE *pc)
{
    unsigned i;
    for (i = rec->bblist.size - 1; i > 0; i--) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
	if (bb->start_pc == pc) {
	    return bb;
	}
    }
    return NULL;
}

/* trace { */
static trace_t *trace_new(jit_event_t *e, trace_side_exit_handler_t *parent)
{
    trace_t *trace = (trace_t *)malloc(sizeof(*trace));
    memset(trace, 0, sizeof(*trace));
    trace->start_pc = e->pc;
    trace->parent = parent;
    if (parent != NULL) {
	trace_link(parent, trace);
    }
    trace->iseq = GET_ISEQ();
    const_pool_init(&trace->cpool);
#if JIT_DEBUG_TRACE
    trace->func_name = NULL;
#endif
    return trace;
}

static void trace_delete(trace_t *trace);

static void trace_reset(trace_t *trace)
{
    if (trace->side_exit) {
	hashmap_dispose(trace->side_exit, (hashmap_entry_destructor_t)trace_delete);
	trace->side_exit = NULL;
    }
    const_pool_delete(&trace->cpool);
    const_pool_init(&trace->cpool);
    trace->counter = 0;
    trace->refc = 0;
#if JIT_DEBUG_TRACE
    if (trace->func_name) {
	free(trace->func_name);
	trace->func_name = NULL;
    }
#endif
}

static void trace_drop_compiled_code(trace_t *trace);

static void trace_delete(trace_t *trace)
{
    rb_jit_t *jit = current_jit;
    if (trace->refc == 0) {
	const_pool_delete(&trace->cpool);
	trace->counter = 0;
	trace_drop_compiled_code(trace);
	jit_list_remove(&jit->trace_list, (uintptr_t)trace);
	if (trace->side_exit) {
	    hashmap_dispose(trace->side_exit, (hashmap_entry_destructor_t)trace_delete);
	    trace->side_exit = NULL;
	}
    }
    // free(trace);
}

static int trace_is_compiled(trace_t *trace)
{
    return trace && trace->handler && trace->code;
}

static void trace_add_child(trace_t *trace, trace_t *child)
{
    if (trace->side_exit == NULL) {
	trace->side_exit = (hashmap_t *)malloc(sizeof(hashmap_t));
	hashmap_init(trace->side_exit, 1);
    }
    hashmap_set(trace->side_exit, (hashmap_data_t)child, (hashmap_data_t)child);
}

static void trace_link(trace_side_exit_handler_t *parent, trace_t *child)
{
    if (parent) {
	trace_t *trace = parent->this_trace;
	if (!trace) {
	    return;
	}
	if (trace_is_compiled(trace)) {
	    LOG("link trace. parent=%p, child=%p", trace, child);
	    parent->child_trace = child;
	    trace_add_child(trace, child);
	}
    }
}

static void trace_unlink(trace_t *trace)
{
    hashmap_iterator_t itr;
    trace_t *buf[32] = {};
    trace_t *root_trace;
    int bufpos = 0;

    root_trace = (trace_t *)hashmap_get(&current_jit->traces, (hashmap_data_t)trace->start_pc);
    buf[bufpos++] = trace;

    while (bufpos != 0) {
	trace = buf[--bufpos];
	itr.entry = 0;
	itr.index = 0;
	if (trace->side_exit) {
	    while (hashmap_next(trace->side_exit, &itr)) {
		trace_t *child = (trace_t *)itr.entry->val;
		child->parent = NULL;
		buf[bufpos++] = child;
	    }
	}
	if (root_trace && root_trace == trace) {
	    LOG("drop trace from root trace set (trace=%p)", trace);
	    hashmap_remove(&current_jit->traces, (hashmap_data_t)trace->start_pc, NULL);
	}
	LOG("drop trace. (trace=%p)", trace);
	trace_delete(trace);
    }
}

static void trace_validate(trace_recorder_t *rec, trace_t *trace)
{
    if (basicblock_get_terminator(rec->cur_bb) == NULL) {
	jit_event_t *e = rec->current_event;
	VALUE *next_pc = e->pc + insn_len(e->opcode);
	trace_recorder_take_snapshot(rec, next_pc, 1);
	Emit_Exit(rec, next_pc);
    }
}

/* } trace */

static void add_blacklist_trace(rb_jit_t *jit, trace_t *trace);

static void compile_trace(rb_jit_t *jit, trace_recorder_t *rec)
{
    trace_t *trace = jit->current_trace;
    if (trace_recorder_inst_size(rec) > LIR_MIN_TRACE_LENGTH) {
	trace_validate(rec, trace);
	// dump_trace(rec);
	trace_optimize(rec, trace);
	dump_trace(rec);
	trace_compile(rec, trace);
    }
    else {
	trace->failed_counter += 1;
	if (trace->failed_counter > BLACKLIST_TRACE_THRESHOLD) {
	    add_blacklist_trace(jit, trace);
	}
	if (JIT_DEBUG_VERBOSE) {
	    fprintf(stderr, "current trace size=%d is too short to compile.\n",
	            trace_recorder_inst_size(rec));
	}
    }
    rec->trace = NULL;
    trace_recorder_clear(rec, NULL, 0);
}
#define TRACE_LOG(EV, FMT, ...)                                       \
    if (JIT_DEBUG_VERBOSE > 1) {                                      \
	jit_event_t *_ev = (EV);                                      \
	const rb_iseq_t *iseq = _ev->cfp->iseq;                       \
	VALUE file = iseq->location.path;                             \
	int line = rb_iseq_line_no(iseq, e->pc - iseq->iseq_encoded); \
	fprintf(stderr, FMT, RSTRING_PTR(file), line, ##__VA_ARGS__); \
    }                                                                 \
    else {                                                            \
    }

static void trace_recorder_abort(trace_recorder_t *rec, jit_event_t *e, enum trace_error_state reason, const char *msg)
{
    rb_jit_t *jit = current_jit;
    if (reason != TRACE_ERROR_OK) {
	e->reason = reason;
	TRACE_LOG(e, "failed to trace at file:%s line:%d because %s. %s\n",
	          trace_error_message[reason], msg);
	trace_recorder_take_snapshot(rec, e->pc, 1);
	Emit_Exit(rec, e->pc);
	if (reason != TRACE_ERROR_REGSTACK_UNDERFLOW) {
	    compile_trace(jit, rec);
	}
    }
    stop_recording(jit);
    rec->trace = NULL;
    trace_recorder_clear(rec, NULL, 0);
}

static variable_table_t *variable_table_init(struct memory_pool *mp, lir_t inst);
static variable_table_t *variable_table_clone(struct memory_pool *mp, variable_table_t *vt);

static basicblock_t *trace_recorder_create_block(trace_recorder_t *rec, VALUE *pc)
{
    basicblock_t *bb = basicblock_new(&rec->mpool, pc);
    basicblock_t *cur_bb = rec->cur_bb;
    bb->base.id = rec->bblist.size;
    jit_list_add(&rec->bblist, (uintptr_t)bb);
    if (!cur_bb) {
	bb->init_table = variable_table_init(&rec->mpool, NULL);
	bb->last_table = variable_table_clone(&rec->mpool, bb->init_table);
    }
    else {
	bb->init_table = variable_table_clone(&rec->mpool, cur_bb->last_table);
	bb->last_table = variable_table_clone(&rec->mpool, bb->init_table);
    }
    return bb;
}

static void trace_recorder_create_entry_block(trace_recorder_t *rec, VALUE *pc)
{
    basicblock_t *bb = trace_recorder_create_block(rec, pc);
    Emit_Jump(rec, bb);
    rec->cur_bb = bb;
}

static void trace_recorder_remove_bb(trace_recorder_t *rec, basicblock_t *bb)
{
    jit_list_remove(&rec->bblist, (uintptr_t)bb);
    basicblock_delete(bb);
}

static lir_t trace_recorder_insert_stackpop(trace_recorder_t *rec)
{
    basicblock_t *entry_bb = rec->entry_bb;
    basicblock_t *cur_bb = rec->cur_bb;
    lir_t reg;
    int need_swap = basicblock_get_terminator(entry_bb) != NULL;
    rec->cur_bb = entry_bb;
    reg = Emit_StackPop(rec);
    if (need_swap) {
	unsigned inst_size = entry_bb->insts.size;
	assert(inst_size >= 2);
	basicblock_swap_inst(entry_bb, inst_size - 2, inst_size - 1);
    }
    rec->cur_bb = cur_bb;
    return reg;
}

static int trace_invoke(rb_jit_t *jit, jit_event_t *e, trace_t *trace)
{
    trace_side_exit_handler_t *handler;
    rb_control_frame_t *cfp = e->cfp;
    rb_thread_t *th = e->th;
    JIT_PROFILE_COUNT(invoke_trace_invoke_enter);
L_head:
    cfp = th->cfp;
    if (JIT_DEBUG_VERBOSE > 0) {
	TRACE_LOG(e, "invoke compiled trace at file:%s line:%d trace=%p\n",
	          trace);
    }

    JIT_PROFILE_COUNT(invoke_trace_invoke);
    trace->refc++;
    handler = trace->code(th, cfp);
#ifdef ENABLE_PROFILE_TRACE_JIT
    trace->invoked++;
#endif
    trace->refc--;
    if (trace->refc == 0 && trace->code == NULL) {
	VALUE *pc = handler->exit_pc;
	trace_delete(trace);
	th->cfp->pc = pc;
	return 1;
    }
    if (JIT_DEBUG_VERBOSE > 0 || JIT_LOG_SIDE_EXIT > 0) {
	TRACE_LOG(e, "[file:%s line:%d] trace %s for %p is exit from %p, cfp=%p\n",
#if JIT_DEBUG_TRACE
	          (trace->func_name) ? trace->func_name : "",
#else
	          "",
#endif
	          e->pc, handler->exit_pc, th->cfp);
    }
    switch (handler->exit_status) {
	case TRACE_EXIT_SIDE_EXIT:
	    JIT_PROFILE_COUNT(invoke_trace_side_exit);
	    if (JIT_ENABLE_LINK_TO_CHILD_TRACE) {
		JIT_PROFILE_COUNT(invoke_trace_child1);
		if (trace_is_compiled(handler->child_trace)) {
		    trace = handler->child_trace;
		    goto L_head;
		}
		mark_trace_header(handler, th);
		JIT_PROFILE_COUNT(invoke_trace_child2);
		if (trace_is_compiled(handler->child_trace)) {
		    trace = handler->child_trace;
		    goto L_head;
		}
	    }
	    break;
	case TRACE_EXIT_SUCCESS:
	    JIT_PROFILE_COUNT(invoke_trace_success);
	    break;
	case TRACE_EXIT_ERROR:
	    assert(0 && "unreachable");
	    break;
    }
    th->cfp->pc = handler->exit_pc;
    return 1;
}

static lir_inst_t *constant_fold_inst(trace_recorder_t *, lir_inst_t *);
static void record_insn(trace_recorder_t *ecorder, jit_event_t *e);
static void update_userinfo(trace_recorder_t *rec, lir_inst_t *inst);
static void dump_lir_inst(lir_inst_t *inst);

static lir_inst_t *trace_recorder_add_inst(trace_recorder_t *recorder, lir_inst_t *inst, unsigned inst_size)
{
    lir_inst_t *newinst = NULL;
    if ((newinst = constant_fold_inst(recorder, inst)) == inst) {
	// when `inst` is able to constant folding, folded `inst`
	// is already inserted by `constant_fold_inst`
	newinst = lir_inst_allocate(recorder, inst, inst_size);
	basicblock_append(recorder->cur_bb, newinst);
	update_userinfo(recorder, newinst);
    }
    if (DUMP_LIR > 1) {
	dump_lir_inst(newinst);
    }
    return newinst;
}

/* } trace_recorder */

/* rb_jit { */
static void jit_runtime_init(struct rb_vm_global_state *global_state_ptr)
{
    unsigned long i;
    memset(&jit_runtime, 0, sizeof(jit_runtime_t));

    jit_runtime.cArray = rb_cArray;
    jit_runtime.cFixnum = rb_cFixnum;
    jit_runtime.cFloat = rb_cFloat;
    jit_runtime.cHash = rb_cHash;
    jit_runtime.cMath = rb_cMath;
    jit_runtime.cRegexp = rb_cRegexp;
    jit_runtime.cTime = rb_cTime;
    jit_runtime.cString = rb_cString;
    jit_runtime.cSymbol = rb_cSymbol;
    jit_runtime.cProc = rb_cProc;

    jit_runtime.cTrueClass = rb_cTrueClass;
    jit_runtime.cFalseClass = rb_cTrueClass;
    jit_runtime.cNilClass = rb_cNilClass;

    jit_runtime._rb_check_array_type = rb_check_array_type;
    jit_runtime._rb_big_plus = rb_big_plus;
    jit_runtime._rb_big_minus = rb_big_minus;
    jit_runtime._rb_big_mul = rb_big_mul;
    jit_runtime._rb_int2big = rb_int2big;
    jit_runtime._rb_str_length = rb_str_length;
    jit_runtime._rb_str_plus = rb_str_plus;
    jit_runtime._rb_str_append = rb_str_append;
    jit_runtime._rb_str_resurrect = rb_str_resurrect;
    jit_runtime._rb_range_new = rb_range_new;
    jit_runtime._rb_hash_new = rb_hash_new;
    jit_runtime._rb_hash_aref = rb_hash_aref;
    jit_runtime._rb_hash_aset = rb_hash_aset;
    jit_runtime._rb_reg_match = rb_reg_match;
    jit_runtime._rb_reg_new_ary = rb_reg_new_ary;
    jit_runtime._rb_ary_new = rb_ary_new;
    jit_runtime._rb_ary_plus = rb_ary_plus;
    jit_runtime._rb_ary_push = rb_ary_push;
    jit_runtime._rb_ary_new_from_values = rb_ary_new_from_values;
    jit_runtime._rb_obj_alloc = rb_obj_alloc;
    jit_runtime._rb_obj_as_string = rb_obj_as_string;

    // internal APIs
    jit_runtime._rb_float_new_in_heap = rb_float_new_in_heap;
    jit_runtime._ruby_float_mod = ruby_float_mod;
    jit_runtime._rb_ary_entry = rb_ary_entry;
    jit_runtime._rb_ary_store = rb_ary_store;
    jit_runtime._rb_ary_resurrect = rb_ary_resurrect;
    jit_runtime._rb_exc_raise = rb_exc_raise;
#if HAVE_RB_GC_GUARDED_PTR_VAL
    jit_runtime._rb_gc_guarded_ptr_val = rb_gc_guarded_ptr_val;
#endif
#if SIZEOF_INT < SIZEOF_VALUE
    jit_runtime._rb_out_of_int = rb_out_of_int;
#endif
    jit_runtime._rb_gvar_get = rb_gvar_get;
    jit_runtime._rb_gvar_set = rb_gvar_set;
    jit_runtime._rb_ivar_set = rb_ivar_set;

    jit_runtime._ruby_current_vm = ruby_current_vm;
    jit_runtime._make_no_method_exception = make_no_method_exception;
#if USE_RGENGC
    jit_runtime._rb_gc_writebarrier_incremental = rb_gc_writebarrier_incremental;
#endif
    jit_runtime._rb_gc_writebarrier_generational = rb_gc_writebarrier_generational;
    jit_runtime._rb_vm_make_proc = rb_vm_make_proc;
    jit_runtime._check_match = check_match;

    jit_runtime.redefined_flag = jit_vm_redefined_flag;

    jit_runtime.global_method_state = global_state_ptr->_global_method_state;
    jit_runtime.global_constant_state = global_state_ptr->_global_constant_state;
    jit_runtime.class_serial = global_state_ptr->_class_serial;

    for (i = 0; i < sizeof(jit_runtime_t) / sizeof(VALUE); i++) {
	assert(((VALUE *)&jit_runtime)[i] != 0
	       && "some field of jit_runtime is not initialized");
    }
}

static void jit_global_default_params_setup(struct rb_vm_global_state *global_state_ptr)
{
    char *disable_jit_ptr;
    disable_jit_ptr = getenv("RUJIT_DISABLE_JIT");
    if (disable_jit_ptr != NULL) {
	int disable_jit_i = atoi(disable_jit_ptr);
	if (RTEST(ruby_verbose)) {
	    fprintf(stderr, "disable_jit=%d\n", disable_jit_i);
	}
	disable_jit = disable_jit_i;
    }
    {
	int i;
	for (i = 0; i < BOP_LAST_; i++) {
	    jit_vm_redefined_flag[i] = global_state_ptr->_ruby_vm_redefined_flag[i];
	}
    }
}

static void jit_default_params_setup(rb_jit_t *jit)
{
}

static void jit_mark(void *ptr)
{
    RUBY_MARK_ENTER("jit");
    if (ptr) {
	unsigned i, j;
	rb_jit_t *jit = (rb_jit_t *)ptr;
	// for (i = 0; i < jit->method_cache.size; i++) {
	//     CALL_INFO ci = (CALL_INFO)jit_list_get(&jit->method_cache, i);
	// }
	for (i = 0; i < jit->trace_list.size; i++) {
	    trace_t *trace = (trace_t *)jit_list_get(&jit->trace_list, i);
	    for (j = 0; j < trace->cpool.list.size; j++) {
		VALUE v = (VALUE)jit_list_get(&trace->cpool.list, j);
		RUBY_MARK_UNLESS_NULL(v);
	    }
	}
    }
    RUBY_MARK_LEAVE("jit");
}

static size_t jit_memsize(const void *ptr)
{
    size_t size = 0;
    if (ptr) {
	rb_jit_t *jit = (rb_jit_t *)ptr;
	size += sizeof(rb_jit_t);
	size += jit->method_cache.size * sizeof(rb_call_info_t);
	size += jit->trace_list.size * sizeof(trace_t);
    }
    return size;
}

static const rb_data_type_t jit_data_type = {
    "JIT",
    {
      jit_mark, NULL, jit_memsize,
    },
    NULL,
    NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

static rb_jit_t *jit_init()
{
    VALUE rb_cJit;
    rb_jit_t *jit = (rb_jit_t *)malloc(sizeof(*jit));
    memset(jit, 0, sizeof(*jit));
    rb_cJit = rb_define_class("Jit", rb_cObject);
    rb_undef_alloc_func(rb_cJit);
    rb_undef_method(CLASS_OF(rb_cJit), "new");
    jit->self = TypedData_Wrap_Struct(rb_cJit, &jit_data_type, jit);
    rb_gc_register_mark_object(jit->self);

    jit->recorder = trace_recorder_new();
    bloom_filter_init(&jit->filter);
    bloom_filter_init(&jit->blacklist_filter);
    hashmap_init(&jit->traces, 1);
    hashmap_init(&jit->blacklist, 1);
    hashmap_init(&jit->block_contained, 1);
    rb_jit_init_redefined_flag();
    jit_default_params_setup(jit);
    jit_list_init(&jit->method_cache);
    jit_list_init(&jit->trace_list);
    jit->main_thread = GET_THREAD();
    return jit;
}

static void jit_remove_trace(hashmap_data_t data)
{
    jit_list_t *trace_list = (jit_list_t *)data;
    unsigned i;
    for (i = 0; i < trace_list->size; i++) {
	trace_t *trace = (trace_t *)jit_list_get(trace_list, i);
	trace_unlink(trace);
    }
    jit_list_delete(trace_list);
}

void rb_jit_notify_proc_freed(void *ptr)
{
    rb_jit_t *jit = current_jit;
    if (jit) {
	hashmap_remove(&jit->block_contained, (hashmap_data_t)ptr, jit_remove_trace);
    }
}

static void jit_regsiter_block(trace_t *trace, const rb_block_t *block)
{
    rb_jit_t *jit = current_jit;
    jit_list_t *trace_list;
    rb_proc_t *proc;

    if (block->proc) {
	hashmap_data_t procaddr;
	GetProcPtr(block->proc, proc);
	procaddr = (hashmap_data_t)proc;
	trace_list = (jit_list_t *)hashmap_get(&jit->block_contained, procaddr);
	if (trace_list == NULL) {
	    trace_list = (jit_list_t *)malloc(sizeof(jit_list_t));
	    jit_list_init(trace_list);
	    hashmap_set(&jit->block_contained, procaddr, (hashmap_data_t)trace_list);
	}
	jit_list_add(trace_list, (hashmap_data_t)trace);
    }
}

static void jit_delete(rb_jit_t *jit)
{
    if (jit) {
	unsigned i;
	hashmap_dispose(&jit->traces, (hashmap_entry_destructor_t)trace_delete);
	hashmap_dispose(&jit->blacklist, NULL);
	hashmap_dispose(&jit->block_contained, (hashmap_entry_destructor_t)jit_list_free);
	trace_recorder_delete(jit->recorder);
	for (i = 0; i < jit->method_cache.size; i++) {
	    free((void *)jit_list_get(&jit->method_cache, i));
	}
	jit_list_delete(&jit->method_cache);
	jit_list_delete(&jit->trace_list);
	free(jit);

#ifdef ENABLE_PROFILE_TRACE_JIT
#define DUMP_COUNT(COUNTER) \
    fprintf(stderr, #COUNTER "  %" PRIu64 "\n", COUNTER)
	DUMP_COUNT(invoke_trace_invoke_enter);
	DUMP_COUNT(invoke_trace_invoke);
	DUMP_COUNT(invoke_trace_child1);
	DUMP_COUNT(invoke_trace_child2);
	DUMP_COUNT(invoke_trace_exit);
	DUMP_COUNT(invoke_trace_side_exit);
	DUMP_COUNT(invoke_trace_success);
	DUMP_COUNT(invoke_bloomfilter_entry);
	DUMP_COUNT(invoke_bloomfilter_hit);
	DUMP_COUNT(invoke_bloomfilter_total);
	DUMP_COUNT(time_trace_search);
#undef DUMP_COUNT
#endif
    }
}

void Init_rawjit(struct rb_vm_global_state *global_state_ptr)
{
    jit_global_default_params_setup(global_state_ptr);
    if (!disable_jit) {
	rb_cMath = rb_singleton_class(rb_mMath);
	rb_gc_register_mark_object(rb_cMath);
	jit_runtime_init(global_state_ptr);
	current_jit = jit_init();
    }
    Init_jit(); // load jit_prelude
}

void Destruct_rawjit()
{
    if (!disable_jit) {
	jit_delete(current_jit);
	current_jit = NULL;
    }
    // remove compiler warnings
    (void)insn_op_types;
    (void)insn_op_type;
}
/* } rb_jit */

static int is_recording(rb_jit_t *jit)
{
    return rujit_record_trace_mode == 1;
}

static void start_recording(rb_jit_t *jit, trace_t *trace)
{
    rujit_record_trace_mode = 1;
    jit->mode |= TraceModeRecording;
    jit->current_trace = trace;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "start recording\n");
    }
}

static void stop_recording(rb_jit_t *jit)
{
    rujit_record_trace_mode = 0;
    jit->mode = TraceModeDefault;
    jit->current_trace = NULL;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "stop recording\n");
    }
}

static trace_t *create_new_trace(rb_jit_t *jit, jit_event_t *e, trace_side_exit_handler_t *parent)
{
    trace_t *trace = trace_new(e, parent);
    jit_list_add(&jit->trace_list, (uintptr_t)trace);
    if (!parent) {
	if (JIT_USE_BLOOM_FILTER) {
	    bloom_filter_add(&jit->filter, (uintptr_t)e->pc);
	}
	hashmap_set(&jit->traces, (hashmap_data_t)e->pc, (hashmap_data_t)trace);
    }
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "allocate new trace %p\n", trace);
    }
    if (JIT_DEBUG_VERBOSE) {
	static int trace_count = 0;
	assert(trace_count++ < MAX_TRACE_SIZE);
    }
    return trace;
}

static trace_t *find_trace(rb_jit_t *jit, jit_event_t *e)
{
    trace_t *trace;
#ifdef ENABLE_PROFILE_TRACE_JIT
    JIT_PROFILE_ENTER("find trace");
#endif
    if (JIT_USE_BLOOM_FILTER) {
	if (!bloom_filter_contains(&jit->filter, (uintptr_t)e->pc)) {
	    return NULL;
	}
    }
    trace = (trace_t *)hashmap_get(&jit->traces, (hashmap_data_t)e->pc);
#ifdef ENABLE_PROFILE_TRACE_JIT
    time_trace_search += JIT_PROFILE_LEAVE("find trace", 0);
#endif
    return trace;
}

static void add_blacklist_trace(rb_jit_t *jit, trace_t *trace)
{
    if (JIT_USE_BLOOM_FILTER) {
	bloom_filter_add(&jit->blacklist_filter, (uintptr_t)trace);
    }
    hashmap_set(&jit->traces, (hashmap_data_t)trace, (hashmap_data_t)trace);
}

static trace_t *find_trace_in_blacklist(rb_jit_t *jit, trace_t *trace)
{
    if (JIT_USE_BLOOM_FILTER) {
	if (!bloom_filter_contains(&jit->blacklist_filter, (uintptr_t)trace)) {
	    return NULL;
	}
    }
    return (trace_t *)hashmap_get(&jit->blacklist, (hashmap_data_t)trace);
}

static void mark_trace_header(trace_side_exit_handler_t *handler, rb_thread_t *th)
{
    rb_control_frame_t *reg_cfp = th->cfp;
    rb_jit_t *jit = current_jit;
    trace_t *trace = NULL;
    jit_event_t ebuf, *e;
    e = jit_event_init(&ebuf, jit, th, reg_cfp, handler->exit_pc);
    if (handler->child_trace) {
	trace = handler->child_trace;
    }
    else if ((trace = find_trace(jit, e)) == NULL) {
	trace = create_new_trace(jit, e, handler);
	trace_reset(trace);
    }
    handler->child_trace = trace;
    if (trace->failed_counter > BLACKLIST_TRACE_THRESHOLD) {
	add_blacklist_trace(jit, trace);
    }
    else if (!trace_is_compiled(trace)) {
	trace_recorder_clear(jit->recorder, trace, 1);
	start_recording(jit, trace);
    }
}

static int is_backward_branch(jit_event_t *e, VALUE **target_pc_ptr)
{
    OFFSET dst;
    if (e->opcode != BIN(branchif)) {
	return 0;
    }
    dst = (OFFSET)GET_OPERAND(1);
    *target_pc_ptr = e->pc + insn_len(BIN(branchif)) + dst;
    return dst < 0;
}

static int already_recorded_on_trace(jit_event_t *e)
{
    rb_jit_t *jit = current_jit;
    trace_side_exit_handler_t *parent = e->trace->parent;
    trace_t *trace;
    if (parent) {
	if (parent->this_trace->start_pc == e->pc) {
	    // linked to other trace
	    trace_recorder_take_snapshot(jit->recorder, REG_PC, 1);
	    Emit_Exit(jit->recorder, e->pc);
	    return 1;
	}
    }
    else if (e->trace->start_pc == e->pc) {
	record_insn(jit->recorder, e);
	return 1;
    }
    //else if (trace_recorder_get_block(jit->recorder, e->pc)) {
    //    record_insn(jit->recorder, e);
    //    return 1;
    //}
    else if ((trace = find_trace(jit, e)) != NULL && trace_is_compiled(trace)) {
	// linked to other trace
	trace_recorder_take_snapshot(jit->recorder, REG_PC, 1);
	Emit_Exit(jit->recorder, e->pc);
	return 1;
    }
    return 0;
}

static int is_irregular_event(jit_event_t *e)
{
    return e->opcode == BIN(throw);
}

static int is_end_of_trace(trace_recorder_t *recorder, jit_event_t *e)
{
    if (already_recorded_on_trace(e)) {
	e->reason = TRACE_ERROR_ALREADY_RECORDED;
	return 1;
    }
    if (trace_recorder_is_full(recorder)) {
	e->reason = TRACE_ERROR_BUFFER_FULL;
	trace_recorder_take_snapshot(recorder, REG_PC, 1);
	if (JIT_DEBUG_VERBOSE) {
	    TRACE_LOG(e, "[file:%s line:%d] exit trace : buffer full\n");
	}
	Emit_Exit(recorder, e->pc);
	return 1;
    }
    //if (!is_tracable_call_inst(e)) {
    //    e->reason = TRACE_ERROR_NATIVE_METHOD;
    //    trace_recorder_take_snapshot(recorder, REG_PC, 1);
    //    return 1;
    //}
    if (is_irregular_event(e)) {
	e->reason = TRACE_ERROR_THROW;
	trace_recorder_take_snapshot(recorder, REG_PC, 1);
	if (JIT_DEBUG_VERBOSE) {
	    TRACE_LOG(e, "[file:%s line:%d] exit trace : : throw error\n");
	}
	Emit_Exit(recorder, e->pc);
	return 1;
    }
    return 0;
}

static int trace_selection(rb_jit_t *jit, jit_event_t *e)
{
    trace_t *trace = NULL;
    VALUE *target_pc = NULL;
    if (is_recording(jit)) {
	if (is_end_of_trace(jit->recorder, e)) {
	    compile_trace(jit, jit->recorder);
	    stop_recording(jit);
	}
	else {
	    record_insn(jit->recorder, e);
	}
	return 0;
    }
    trace = find_trace(jit, e);
    if (trace_is_compiled(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (is_backward_branch(e, &target_pc)) {
	if (!trace) {
	    trace = create_new_trace(jit, e, NULL);
	}
	trace->last_pc = target_pc;
    }
    if (trace) {
	trace->counter += 1;
	if (trace->counter > HOT_TRACE_THRESHOLD) {
	    if (find_trace_in_blacklist(jit, trace)) {
		return 0;
	    }
	    start_recording(jit, trace);
	    trace_reset(trace);
	    trace_recorder_clear(jit->recorder, trace, 1);
	    trace_recorder_create_entry_block(jit->recorder, target_pc);
	    // record_insn(jit->recorder, e);
	}
    }
    return 0;
}

int rujit_invoke_or_make_trace(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc, int opcode)
{
    jit_event_t ebuf, *e;
    rb_jit_t *jit = current_jit;
    if (UNLIKELY(disable_jit)) {
	return 0;
    }
    // FIXME rujit do not support multi thread
    if (UNLIKELY(th != current_jit->main_thread)) {
	return 0;
    }

    reg_pc -= insn_len((VALUE)opcode);
    if (is_recording(jit)) {
	return 0;
    }
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    return trace_selection(current_jit, e);
}

void rujit_record_insn(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    rb_jit_t *jit = current_jit;
    jit_event_t ebuf, *e;
    if (UNLIKELY(disable_jit)) {
	return;
    }
    // FIXME rujit do not support multi thread
    if (UNLIKELY(th != current_jit->main_thread)) {
	return;
    }

    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    assert(is_recording(jit));
    trace_selection(jit, e);
}

#include "jit_record.c"
#include "jit_optimize.c"
#include "jit_codegen.c"
