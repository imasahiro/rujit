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
#include <dlfcn.h> // dlopen, dlclose, dlsym

#define JIT_HOST 1
#include "jit.h"
#include "jit_internal.h"
#include "jit_config.h"
#include "jit_prelude.c"
#include "jit_cgen_cmd.h" // static const char cmd_template[];

#undef REG_CFP
#undef REG_PC
#define REG_CFP ((e)->cfp)
#define REG_PC ((e)->pc)

typedef struct rujit_core rujit_t;
typedef struct jit_event_t jit_event_t;
typedef struct jit_trace_t jit_trace_t;

/* global variables */
int rujit_record_trace_mode = 0;
static int disable_jit = 0;
jit_runtime_t jit_runtime = {};
static VALUE rb_cMath = Qnil;
static VALUE rb_cJit = Qnil;
static rujit_t *current_jit = NULL;

#include "jit_utils.h"
#include "jit_ruby_api.h"

/* RuJIT core */

typedef struct lir_func_t lir_func_t;

typedef struct lir_builder_t {
    struct lir_func_t *cur_func;
    struct basicblock_t *cur_bb;
    struct memory_pool_t *mpool;
} lir_builder_t;

typedef struct native_func_manager_t {
} native_func_manager_t;

struct rujit_core {
    VALUE self;
    rb_thread_t *main_thread;
    jit_event_t *current_event;
    struct trace_t *current_trace;
    struct rujit_backend_t *backend;
    struct trace_recorder_t *recorder;
    memory_pool_t mpool;
    lir_builder_t builder;
    native_func_manager_t manager;
    hashmap_t traces;
    hashmap_t blacklist;
    bloom_filter_t filter;
    bloom_filter_t blacklist_filter;
};

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

static void rujit_global_default_params_setup(struct rb_vm_global_state *ptr)
{
    int i;
    char *disable_jit_ptr;
    disable_jit_ptr = getenv("RUJIT_DISABLE_JIT");
    if (disable_jit_ptr != NULL) {
	int disable_jit_i = atoi(disable_jit_ptr);
	if (RTEST(ruby_verbose)) {
	    fprintf(stderr, "disable_jit=%d\n", disable_jit_i);
	}
	disable_jit = disable_jit_i;
    }
    for (i = 0; i < BOP_LAST_; i++) {
	jit_vm_redefined_flag[i] = ptr->_ruby_vm_redefined_flag[i];
    }
}

static void jit_default_params_setup(rujit_t *jit)
{
}

static void jit_mark(void *ptr)
{
    RUBY_MARK_ENTER("jit");
    if (ptr) {
    }
    RUBY_MARK_LEAVE("jit");
}

static size_t jit_memsize(const void *ptr)
{
    size_t size = 0;
    if (ptr) {
	rujit_t *jit = (rujit_t *)ptr;
	size += sizeof(rujit_t);
	size += native_func_manager_memsize(&jit->manager);
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

static rujit_t *jit_new()
{
    rujit_t *jit = (rujit_t *)malloc(sizeof(*jit));
    memset(jit, 0, sizeof(*jit));
    if (rb_cJit == Qnil) {
	rb_cJit = rb_define_class("Jit", rb_cObject);
	rb_undef_alloc_func(rb_cJit);
	rb_undef_method(CLASS_OF(rb_cJit), "new");
	jit->self = TypedData_Wrap_Struct(rb_cJit, &jit_data_type, jit);
	rb_gc_register_mark_object(jit->self);
    }

    jit->recorder = trace_recorder_new();
    bloom_filter_init(&jit->filter);
    bloom_filter_init(&jit->blacklist_filter);
    native_func_manager_init(&jit->manager);
    hashmap_init(&jit->traces, 1);
    hashmap_init(&jit->blacklist, 1);
    rb_jit_init_redefined_flag();
    jit_default_params_setup(jit);
    jit->main_thread = GET_THREAD();
    return jit;
}

static void jit_delete(rujit_t *jit)
{
    unsigned i;
    assert(jit);
    //hashmap_dispose(&jit->traces, (hashmap_entry_destructor_t)trace_delete);
    hashmap_dispose(&jit->blacklist, NULL);
    trace_recorder_delete(jit->recorder);
    native_func_manager_delete(&jit->manager);
    jit_profile_dump();
    free(jit);
}

static int is_recording(rujit_t *jit)
{
    return rujit_record_trace_mode == 1;
}

static void start_recording(rujit_t *jit, jit_trace_t *trace)
{
    rujit_record_trace_mode = 1;
    jit->current_trace = trace;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "start recording\n");
    }
}

static void stop_recording(rujit_t *jit)
{
    rujit_record_trace_mode = 0;
    jit->current_trace = NULL;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "stop recording\n");
    }
}

void Init_rawjit(struct rb_vm_global_state *global_state_ptr)
{
    rujit_global_default_params_setup(global_state_ptr);
    if (!disable_jit) {
	rb_cMath = rb_singleton_class(rb_mMath);
	rb_gc_register_mark_object(rb_cMath);
	jit_runtime_init(global_state_ptr);
	current_jit = jit_new();
    }
    Init_jit(); // load jit_prelude
}

void Destruct_rawjit()
{
    if (!disable_jit) {
	jit_delete(current_jit);
	current_jit = NULL;
    }
}

/* lir_inst {*/
typedef struct lir_inst_t {
    unsigned id;
    unsigned short opcode;
    unsigned short flag;
    struct lir_basicblock_t *parent;
    jit_list_t *user;
} lir_inst_t, *lir_t;

static lir_t lir_init(lir_t inst, size_t size, unsigned opcode)
{
    memset(inst, 0, size);
    inst->opcode = opcode;
    return opcode;
}

static void lir_delete(lir_t inst)
{
    if (inst->user) {
	jit_list_delete(inst->user);
	inst->user = NULL;
    }
}

#define LIR_NEWINST(T) ((T *)lir_inst_init(alloca(sizeof(T)), sizeof(T), OPCODE_##T))
#define LIR_NEWINST_N(T, SIZE) \
    ((T *)lir_inst_init(alloca(sizeof(T) + sizeof(lir_t) * (SIZE)), sizeof(T) + sizeof(lir_t) * (SIZE), OPCODE_##T))

/* } lir_inst */

#define ADD_INST(REC, INST) ADD_INST_N(REC, INST, 0)

#define ADD_INST_N(REC, INST, SIZE) \
    trace_recorder_add_inst(REC, &(INST)->base, sizeof(*INST) + sizeof(lir_t) * (SIZE))

#include "lir.c"

static int lir_is_terminator(lir_t inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_TERMINATOR_##OPNAME;
	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static int lir_is_guard(lir_t inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_GUARD_INST_##OPNAME;
	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static lir_t *lir_inst_get_args(lir_t inst, int idx)
{
#define GET_ARG(OPNAME)    \
    case OPCODE_I##OPNAME: \
	return GetNext_##OPNAME(inst, idx);

    switch (lir_opcode(inst)) {
	LIR_EACH(GET_ARG);
	default:
	    assert(0 && "unreachable");
    }
#undef GET_ARG
    return NULL;
}

static void lir_inst_adduser(trace_recorder_t *rec, lir_t inst, lir_t ir)
{
    if (inst->user == NULL) {
	inst->user = (jit_list_t *)memory_pool_alloc(&rec->mpool, sizeof(jit_list_t));
	jit_list_init(inst->user);
    }
    jit_list_add(inst->user, (uintptr_t)ir);
}

static void lir_inst_removeuser(lir_t inst, lir_t ir)
{
    if (inst->user == NULL) {
	return;
    }
    jit_list_remove(inst->user, (uintptr_t)ir);
    if (inst->user->size == 0) {
	jit_list_delete(inst->user);
	inst->user = NULL;
    }
}

static void lir_update_userinfo(trace_recorder_t *rec, lir_t inst)
{
    lir_t *ref = NULL;
    int i = 0;
    while ((ref = lir_inst_get_args(inst, i)) != NULL) {
	lir_t user = *ref;
	if (user) {
	    lir_inst_adduser(rec, user, inst);
	}
	i += 1;
    }
}

/* basicblock { */
typedef struct lir_basicblock_t {
    lir_inst_t base;
    VALUE *pc;
    struct local_var_table_t *init_table;
    struct local_var_table_t *last_table;
    jit_list_t insts;
    jit_list_t preds;
    jit_list_t succs;
    jit_list_t side_exits; // n -> PC, n+1 -> reg2stack map
} basicblock_t;

static basicblock_t *basicblock_new(memory_pool_t *mpool, VALUE *pc, int id)
{
    basicblock_t *bb = (basicblock_t *)memory_pool_alloc(mpool, sizeof(*bb));
    bb->pc = pc;
    bb->init_table = NULL;
    bb->last_table = NULL;
    jit_list_init(&bb->insts);
    jit_list_init(&bb->preds);
    jit_list_init(&bb->succs);
    jit_list_init(&bb->side_exits);
    return bb;
}

static unsigned basicblock_size(basicblock_t *bb)
{
    return bb->insts.size;
}

static void basicblock_delete(basicblock_t *bb)
{
    unsigned i;
    for (i = 0; i < basicblock_size(bb); i++) {
	lir_t inst = JIT_LIST_GET(lir_t, &bb->insts, i);
	lir_delete(inst);
    }
    jit_list_delete(&bb->insts);
    jit_list_delete(&bb->preds);
    jit_list_delete(&bb->succs);
    jit_list_delete(&bb->side_exits);

    if (bb->init_table) {
	local_var_table_delete(bb->init_table);
    }
    if (bb->last_table) {
	local_var_table_delete(bb->last_table);
    }
}

static void basicblock_link(basicblock_t *bb, basicblock_t *child)
{
    JIT_LIST_ADD(&bb->succs, child);
    JIT_LIST_ADD(&child->preds, bb);
}

static basicblock_t *basicblock_get_succ(basicblock_t *bb, unsigned idx)
{
    return JIT_LIST_GET(basicblock_t *, &bb->succs, idx);
}

static basicblock_t *basicblock_get_pred(basicblock_t *bb, unsigned idx)
{
    return JIT_LIST_GET(basicblock_t *, &bb->preds, idx);
}

static void basicblock_unlink(basicblock_t *bb, basicblock_t *child)
{
    JIT_LIST_REMOVE(&bb->succs, child);
    JIT_LIST_REMOVE(&child->preds, bb);
}

static void basicblock_insert_inst_before(basicblock_t *bb, lir_t target, lir_t val)
{
    int idx1 = JIT_LIST_INDEXOF(&bb->insts, target);
    int idx2 = JIT_LIST_INDEXOF(&bb->insts, val);
    assert(idx1 >= 0 && idx2 >= 0);
    JIT_LIST_REMOVE(&bb->insts, val);
    JIT_LIST_INSERT(&bb->insts, idx1, val);
}

static void basicblock_insert_inst_after(basicblock_t *bb, lir_t target, lir_t val)
{
    int idx1 = JIT_LIST_INDEXOF(&bb->insts, target);
    int idx2 = JIT_LIST_INDEXOF(&bb->insts, val);
    assert(idx1 >= 0 && idx2 >= 0);
    JIT_LIST_REMOVE(&bb->insts, val);
    JIT_LIST_INSERT(&bb->insts, idx1 + 1, val);
}

static void basicblock_swap_inst(basicblock_t *bb, int idx1, int idx2)
{
    lir_t inst1 = JIT_LIST_GET(lir_t, &bb->insts, idx1);
    lir_t inst2 = JIT_LIST_GET(lir_t, &bb->insts, idx2);
    JIT_LIST_SET(&bb->insts, idx1, inst2);
    JIT_LIST_SET(&bb->insts, idx2, inst1);
}

static void basicblock_append(basicblock_t *bb, lir_inst_t *inst)
{
    JIT_LIST_ADD(&bb->insts, inst);
    inst->parent = bb;
}

static lir_t basicblock_get(basicblock_t *bb, int i)
{
    return JIT_LIST_GET(lir_t, &bb->insts, i);
}

static int basicblock_get_index(basicblock_t *bb, lir_t inst)
{
    return JIT_LIST_INDEXOF(&bb->insts, inst);
}

static lir_t basicblock_get_last(basicblock_t *bb)
{
    lir_t val;
    if (basicblock_size(bb) == 0) {
	return NULL;
    }
    return basicblock_get(bb, basicblock_size(bb) - 1);
}

static lir_t basicblock_get_terminator(basicblock_t *bb)
{
    lir_t val = basicblock_get_last(bb);
    if (lir_is_terminator(val)) {
	return val;
    }
    return NULL;
}

static lir_t basicblock_get_next(basicblock_t *bb, lir_t inst)
{
    int idx = basicblock_get_index(bb, inst);
    if (0 <= idx && idx - 1 < (int)basicblock_size(bb)) {
	return basicblock_get(bb, idx + 1);
    }
    return NULL;
}

/* } basicblock */

/* lir_func { */
struct lir_func_t {
    basicblock_t *entry_bb;
    jit_list_t constants;
    jit_list_t method_cache;
    jit_list_t bblist;
    jit_list_t side_exits; /* Array<side_exit_handler_t*> */
};

static lir_func_t *lir_func_new(memory_pool_t *mp)
{
    lir_func_t *func = MEMORY_POOL_ALLOC(lir_func_t, mp);
    jit_list_init(&func->bblist);
    jit_list_init(&func->side_exits);
    return func;
}

static void lir_func_delete(lir_func_t *func)
{
    jit_list_delete(&func->constants);
    jit_list_delete(&func->method_cache);
    jit_list_delete(&func->bblist);
    jit_list_delete(&func->side_exits);
}
/* } lir_func */

/* lir_builder {*/
static lir_builder_t *lir_builder_new(memory_pool_t *mpool, lir_func_t *func)
{
    lir_builder_t *builder = (lir_builder_t *)malloc(sizeof(lir_builder_t));
    builder->mpool = mpool;
    builder->cur_func = func;
    builder->cur_bb = NULL;
    return builder;
}

static basicblock_t *lir_builder_create_block(lir_builder_t *builder, VALUE *pc)
{
    return basicblock_new(builder->mpool, pc);
}

static lir_t lir_builder_add_inst(lir_builder_t *builder, lir_t val)
{
    basicblock_append(builder->cur_bb, val);
    return val;
}

static jit_list_t *lir_builder_cur_bb(lir_builder_t *builder)
{
    return builder->cur_bb;
}

static jit_list_t *lir_builder_blocks(lir_builder_t *builder)
{
    return builder->cur_func->bblist;
}

static basicblock_t *lir_builder_get_block(lir_builder_t *builder, unsigned idx)
{
    jit_list_t *bblist = lir_builder_blocks(builder);
    return JIT_LIST_GET(basicblock_t, bblist, idx);
}

static void lir_builder_delete(lir_builder_t *builder)
{
    free(builder);
}
/* } lir_builder */

typedef trace_side_exit_handler_t *(*native_raw_func_t)(rb_thread_t *, rb_control_frame_t *);

typedef struct native_func_t {
    unsigned flag;
    unsigned refc;
    void *handler;
    native_raw_func_t code;
    lir_func_t *origin;
} native_func_t;

struct rujit_backend_t {
    void *ctx;
    void (*f_init)(rujit_t *, struct rujit_backend_t *self);
    void (*f_delete)(rujit_t *, struct rujit_backend_t *self);
    native_raw_func_t *(*f_compile)(rujit_t *, void *ctx, lir_func_t *func);
    void (*f_unload)(rujit_t *, void *ctx, native_func_t *func);
} rujit_backend_t;

static void dummy_init(rujit_t *jit, struct rujit_backend_t *self)
{
}

static void dummy_delete(rujit_t *jit, struct rujit_backend_t *self)
{
}

static native_raw_func_t *dummy_compile(rujit_t *jit, void *ctx, lir_func_t *func)
{
    return NULL;
}

static void dummy_unload(rujit_t *jit, void *ctx, native_func_t *func)
{
}

rujit_backend_t backend_cgen = {
    NULL,
    dummy_init,
    dummy_delete,
    dummy_compile,
    dummy_unload
};

#define RC_INC(O) ((O)->refc++)
#define RC_DEC(O) (--(O)->refc)
#define RC_INIT(O) ((O)->refc = 1)
#define RC_CHECK0(O) ((O)->refc == 0)

static native_func_t *native_func_new(lir_func_t *origin)
{
    native_func_t *func = (native_func_t *)malloc(sizeof(native_func_t));
    func->flag = 0;
    RC_INIT(func);
    func->handler = NULL;
    func->code = NULL;
    func->origin = origin;
    return func;
}

static void native_func_invalidate(native_func_t *func)
{
    rujit_t *jit = current_jit;
    jit->backend->f_unload(jit, jit->backend->ctx, func);
    lir_func_invalidate(func->origin);
    func->origin = NULL;
}

static void native_func_delete(native_func_t *func)
{
    RC_DEC(func);
    assert(RC_CHECK0(func));
    free(func);
}

enum native_func_call_status {
    NATIVE_FUNC_ERROR = 0,
    NATIVE_FUNC_SUCCESS = 1,
    NATIVE_FUNC_DELETED = 1 << 1,
    NATIVE_FUNC_INVALIDATED = 1 << 2
};

static int native_func_invoke(native_func_t *func, VALUE *return_value)
{
    *return_value = 0;
    RC_INC(func);
    *return_value = ((native_raw_func_t)func->code)();
    RC_DEC(func);
    if (func->flag & NATIVE_FUNC_INVALIDATED && RC_CHECK0(func)) {
	native_func_delete(func);
	return NATIVE_FUNC_DELETED;
    }
    return NATIVE_FUNC_SUCCESS;
}

struct jit_trace_t {
    VALUE *start_pc;
    VALUE *last_pc;
    struct native_func_t *func;
    struct jit_trace_t *parent;
    struct jit_trace_t *child;
    long counter;
    long faile_counter;
#ifdef ENABLE_PROFILE_TRACE_JIT
    long invoke_counter;
#endif
#if RUJIT_DEBUG_MODE
    const rb_iseq_t *iseq;
#endif
#if JIT_DEBUG_TRACE
    char *func_name;
#endif
};

static jit_trace_t *trace_new(memory_pool_t *mpool, VALUE *pc, struct jit_trace_t *parent)
{
    jit_trace_t *trace = MEMORY_POOL_ALLOC(jit_trace_t, mpool);
    memset(trace, 0, sizeof(*trace));
    trace->start_pc = pc;
    trace->parent = parent;
    return trace;
}
static void trace_delete(jit_trace_t *trace)
{
    if (trace->func) {
	native_func_invalidate(trace->func);
	trace->func = NULL;
    }
    if (trace->child) {
    }
}

#include "lir.c"
#include "jit_codegen.h"

typedef struct trace_recorder_t {
    struct lir_builder_t *builder;
    struct local_var_table_t *lvar;
    memory_pool_t *mpool;
} trace_recorder_t;

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

static void trace_recorder_init(trace_recorder_t *recorder, memory_pool_t *mpool)
{
    lir_builder_init(&recorder->builder, mpool);
    recorder->lvar = NULL;
    recorder->mpool = mpool;
}

static trace_recorder_t *trace_recorder_new(memory_pool_t *mpool)
{
    trace_recorder_t *recorder = MEMORY_POOL_ALLOC(trace_recorder_t, mpool);
    trace_recorder_init(recorder, mpool);
    return recorder;
}

static void trace_recorder_delete(trace_recorder_t *recorder)
{
    lir_builder_delete(&recorder->builder);
    if (recorder->lvar) {
	local_var_table_delete(recorder->lvar);
    }
}

static void trace_recorder_abort(trace_recorder_t *recorder, enum trace_error_state reason)
{
    asm volatile("int3");
    if (reason != TRACE_ERROR_OK) {
	fprintf(stderr, "%s¥n", trace_error_message[(int)reason]);
    }
}

struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    struct trace_t *trace;
    int opcode;
    enum trace_error_state reason;
};

static jit_event_t *jit_event_init(jit_event_t *e, rujit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
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
    e->reason = TRACE_ERROR_OK;
    jit->current_event = e;
    return e;
}

// record/invoke trace
void rujit_record_insn(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    jit_event_t ebuf, *e;
    rujit_t *jit = current_jit;
    trace_recorder_t *recorder = jit->recorder;
    if (UNLIKELY(disable_jit || th != jit->main_thread)) {
	return;
    }
    assert(is_recording(jit));
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    if (is_end_of_trace(recorder, e)) {
	trace_recorder_compile(jit, recorder);
	stop_recording(jit);
    }
    else {
	record_insn(recorder, e);
    }
}

int rujit_invoke_or_make_trace(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    jit_event_t ebuf, *e;
    rujit_t *jit = current_jit;
    jit_trace_t *trace;
    if (UNLIKELY(disable_jit || th != jit->main_thread)) {
	return 0;
    }
    if (is_recording(jit)) {
	return 0;
    }
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    trace = find_trace(jit, e);

    if (trace_is_compiled(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (is_backward_branch(e)) {
	if (trace == NULL) {
	    trace = rujit_alloc_trace(jit, e, NULL);
	}
	trace->start_pc = reg_pc;
    }

    if (trace) {
	trace->counter += 1;
	if (trace->counter > HOT_TRACE_THRESHOLD) {
	    trace_recorder_t *recorder = jit->recorder;
	    if (find_trace_in_blacklist(jit, trace)) {
		return 0;
	    }
	    start_recording(jit, trace);
	    trace_reset(trace);
	    trace_recorder_reset(recorder, trace, 1);
	    trace_recorder_create_entry_block(recorder, reg_pc);
	    record_insn(recorder, e);
	}
    }
    return 0;
}

// compile method
void rujit_push_compile_queue(rb_thread_t *th, rb_control_frame_t *cfp, rb_method_entry_t *me)
{
    assert(0 && "not implemented");
}

#include "jit_record.h"
#include "jit_codegen.h"
#include "jit_optimize.h"
#include "jit_args.h"
