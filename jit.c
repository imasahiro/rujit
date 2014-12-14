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
#include "jit_utils.h"

#undef REG_CFP
#undef REG_PC
#define REG_CFP ((e)->cfp)
#define REG_PC ((e)->pc)

#define __int3__ __asm__ __volatile__("int3")
#define TODO(MSG)                                                  \
    fprintf(stderr, "TODO(%s:%d): %s\n", __func__, __LINE__, MSG); \
    __int3__;

static VALUE rb_cMath = Qnil;
static VALUE rb_cJit = Qnil;
static int disable_jit = 0;
jit_runtime_t jit_runtime = {};

#include "jit_ruby_api.h"

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

typedef struct jit_event_t {
    rb_thread_t *th;
    rb_control_frame_t *cfp;
    VALUE *pc;
    int opcode;
    enum trace_error_state reason;
} jit_event_t;

enum lir_builder_state {
    LIR_BUILDER_STATE_NOP = 0,
    LIR_BUILDER_STATE_RECORDING = 1,
    LIR_BUILDER_STATE_COMPILING = 2,
    LIR_BUILDER_STATE_ERROR = -1
};

typedef struct jit_trace_t {
    VALUE *start_pc;
    VALUE *last_pc;
    long counter;
    struct native_func_t *native_func;
    struct lir_func_t *lir_func;
} jit_trace_t;

typedef VALUE *(*native_raw_func_t)(rb_thread_t *, rb_control_frame_t *);

typedef struct native_func_t {
    unsigned flag;
    unsigned refc;
    void *handler;
    native_raw_func_t code;
} native_func_t;

typedef struct lir_builder_t {
    enum lir_builder_state mode;
    memory_pool_t *mpool;
    struct lir_func_t *cur_func;
    struct lir_basicblock_t *cur_bb;
    jit_list_t shadow_stack;
    hashmap_t const_pool;
    unsigned inst_size;
} lir_builder_t;

typedef struct native_func_manager_t {
    hashmap_t traces;
    jit_list_t compiled_codes;
    hashmap_t valid_code0;
    hashmap_t valid_code1;
} native_func_manager_t;

typedef struct rujit_t {
    rb_thread_t *main_thread;
    jit_event_t *current_event;
    struct rujit_backend_t *backend;
    lir_builder_t builder;
    memory_pool_t mpool;
    native_func_manager_t manager;
    unsigned func_id;
} rujit_t;

static rujit_t *current_jit;

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

#include "jit_core.h"

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
	size += sizeof(rujit_t);
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

static lir_builder_t *lir_builder_init(lir_builder_t *, memory_pool_t *);

static rujit_t *jit_new()
{
    rujit_t *jit = (rujit_t *)malloc(sizeof(*jit));
    memset(jit, 0, sizeof(*jit));
    jit->main_thread = GET_THREAD();
    memory_pool_init(&jit->mpool);
    lir_builder_init(&jit->builder, &jit->mpool);
    native_func_manager_init(&jit->manager);
    if (RTEST(rb_cJit)) {
	TODO("rb_cJit");
	rb_cJit = Qnil;
    }
    jit->func_id = 0;
    return jit;
}

static void jit_delete(rujit_t *jit)
{
    jit_profile_dump();
    free(jit);
}

static jit_trace_t *jit_find_trace(rujit_t *jit, jit_event_t *e)
{
    return native_func_manager_find_trace(&jit->manager, e->pc);
}

static jit_trace_t *jit_alloc_trace(rujit_t *jit, jit_event_t *e, jit_trace_t *parent)
{
    jit_trace_t *trace = (jit_trace_t *)malloc(sizeof(*trace));
    trace->start_pc = e->pc;
    trace->last_pc = NULL;
    trace->counter = 0;
    trace->native_func = NULL;
    trace->lir_func = NULL;
    native_func_manager_add_trace(&jit->manager, trace);
    return trace;
}

static int trace_invoke(rujit_t *jit, jit_event_t *e, jit_trace_t *trace)
{
    TODO("");
    return 0;
}

static int trace_is_compiled(jit_trace_t *trace)
{
    return trace && trace->native_func != NULL;
}

void Init_rawjit(struct rb_vm_global_state *global_state_ptr)
{
    jit_global_default_params_setup(global_state_ptr);
    if (!disable_jit) {
	rb_cMath = rb_singleton_class(rb_mMath);
	rb_gc_register_mark_object(rb_cMath);
	jit_runtime_init(global_state_ptr);
	current_jit = jit_new();
	Init_jit(); // load jit_prelude
    }
}

void Destruct_rawjit()
{
    if (current_jit) {
	jit_delete(current_jit);
	current_jit = NULL;
    }
}

static jit_event_t *jit_event_init(jit_event_t *e, rujit_t *jit, rb_thread_t *th, rb_control_frame_t *cfp, VALUE *pc)
{
    ISEQ iseq = cfp->iseq;
    VALUE *iseq_orig = rb_iseq_original_iseq(iseq);
    long offset = pc - iseq->iseq_encoded;
    int opcode = (int)iseq_orig[offset];
    e->th = th;
    e->cfp = cfp;
    e->pc = pc;
    e->opcode = opcode;
    e->reason = TRACE_ERROR_OK;
    jit->current_event = e;
    return e;
}

int rujit_record_trace_mode = 0;

static int is_recording()
{
    return rujit_record_trace_mode == 1;
}

static void start_recording()
{
    rujit_record_trace_mode = 1;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "start recording\n");
    }
}

static void stop_recording()
{
    rujit_record_trace_mode = 0;
    if (JIT_DEBUG_VERBOSE) {
	fprintf(stderr, "stop recording\n");
    }
}

static void record_insn(lir_builder_t *, jit_event_t *e);

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
    rujit_t *jit = current_jit;
    jit_trace_t *trace;
    basicblock_t *bb;
    if ((bb = lir_builder_find_block(&jit->builder, e->pc))) {
	if (basicblock_size(bb) != 0) {
	    return 1;
	}
    }
    else if ((trace = jit_find_trace(jit, e)) != NULL) {
	if (trace_is_compiled(trace)) {
	    TODO("");
	    // linked to other trace
	    // lir_builder_take_snapshot(&jit->builder, REG_PC, 1);
	    // Emit_Exit(&jit->builder, e->pc);
	    return 1;
	}
    }
    return 0;
}

static int is_irregular_event(jit_event_t *e)
{
    return e->opcode == BIN(throw);
}

static int is_end_of_trace(lir_builder_t *builder, jit_event_t *e)
{
    if (already_recorded_on_trace(e)) {
	e->reason = TRACE_ERROR_ALREADY_RECORDED;
	return 1;
    }
    if (lir_builder_is_full(builder)) {
	e->reason = TRACE_ERROR_BUFFER_FULL;
	TODO("");
	// lir_builder_take_snapshot(builder, REG_PC, 1);
	// if (JIT_DEBUG_VERBOSE) {
	//     TRACE_LOG(e, "[file:%s line:%d] exit trace : buffer full\n");
	// }
	// Emit_Exit(builder, e->pc);
	return 1;
    }
    //if (!is_tracable_call_inst(e)) {
    //    e->reason = TRACE_ERROR_NATIVE_METHOD;
    //    lir_builder_take_snapshot(builder, REG_PC, 1);
    //    return 1;
    //}
    if (is_irregular_event(e)) {
	e->reason = TRACE_ERROR_THROW;
	TODO("");
	// lir_builder_take_snapshot(builder, REG_PC, 1);
	// if (JIT_DEBUG_VERBOSE) {
	//     TRACE_LOG(e, "[file:%s line:%d] exit trace : : throw error\n");
	// }
	// Emit_Exit(builder, e->pc);
	return 1;
    }
    return 0;
}

// record/invoke trace
void rujit_record_insn(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    jit_event_t ebuf, *e;
    rujit_t *jit = current_jit;
    lir_builder_t *builder = &jit->builder;
    if (UNLIKELY(disable_jit || th != jit->main_thread)) {
	return;
    }
    assert(is_recording());
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    if (is_end_of_trace(builder, e)) {
	lir_builder_compile(jit, builder);
	stop_recording();
    }
    else {
	record_insn(builder, e);
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
    if (is_recording()) {
	return 0;
    }
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    trace = jit_find_trace(jit, e);

    if (trace_is_compiled(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (trace == NULL) {
	trace = jit_alloc_trace(jit, e, NULL);
    }

    trace->start_pc = reg_pc;
    trace->counter += 1;
    if (trace->counter > HOT_TRACE_THRESHOLD) {
	//if (find_trace_in_blacklist(jit, trace)) {
	//    return 0;
	//}
	start_recording();
	lir_builder_reset(&jit->builder, &trace->lir_func, trace->start_pc);
    }
    return 0;
}

// compile method
void rujit_push_compile_queue(rb_thread_t *th, rb_control_frame_t *cfp, rb_method_entry_t *me)
{
    assert(0 && "not implemented");
}

#include "jit_dump.h"
#include "jit_record.h"
#include "jit_codegen.h"
// #include "jit_optimize.h"
#include "jit_args.h"
