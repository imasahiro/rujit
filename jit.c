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
#define _TXT(X) #X
#define TRACE_ERROR_INFO(OP, TAIL)                                         \
    OP(OK, "ok")                                                           \
    OP(NATIVE_METHOD, "invoking native method")                            \
    OP(THROW, "throw exception")                                           \
    OP(ARGUMENT, "argument error")                                         \
    OP(UNSUPPORT_OP, "not supported bytecode")                             \
    OP(LEAVE, "this trace return into native method")                      \
    OP(REGSTACK_UNDERFLOW, "register stack underflow")                     \
    OP(ALREADY_RECORDED, "this instruction is already recorded on trace")  \
    OP(BUFFER_FULL, "trace buffer is full")                                \
    OP(SUBMIT, "this trace is already submitted to compile queue")         \
    OP(CALL_FRAME_FULL, "invoking blk or mtd >= " _TXT(MAX_CALL_FRAME) "") \
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
    int opcode;
    enum trace_error_state reason;
} jit_event_t;

enum lir_builder_state {
    LIR_BUILDER_STATE_NOP = 0,
    LIR_BUILDER_STATE_RECORDING = 1,
    LIR_BUILDER_STATE_COMPILING = 2,
    LIR_BUILDER_STATE_PUSH_QUEUE = 3,
    LIR_BUILDER_STATE_ERROR = -1
};

typedef struct jit_trace_t {
    VALUE *start_pc;
    VALUE *last_pc;
    long counter;
    long failed_counter;
    struct native_func_t *native_func;
    struct lir_func_t *lir_func;
    struct jit_trace_t *parent;
    jit_list_t exit_handlers; /*List<TraceSideExitHandler*>*/
} jit_trace_t;

typedef trace_side_exit_handler_t *(*native_raw_func_t)(rb_thread_t *, rb_control_frame_t *);

typedef struct native_func_t {
    unsigned flag;
    short refc;
    void *handler;
    struct lir_func_t *origin;
    native_raw_func_t code;
#if JIT_DEBUG_TRACE
    char *func_name;
#endif
    size_t invoked;
} native_func_t;

typedef struct lir_builder_t {
    enum lir_builder_state mode;
    memory_pool_t *mpool;
    struct lir_func_t *cur_func;
    struct lir_basicblock_t *cur_bb;
    struct jit_trace_t *cur_trace;
    jit_list_t shadow_stack;
    jit_list_t stack_ops;
    jit_list_t call_frame;
    jit_list_t call_caches;
    jit_list_t func_list;
    unsigned inst_size;
    unsigned block_size;
} lir_builder_t;

typedef struct native_func_manager_t {
    hashmap_t traces;
    jit_list_t compiled_codes;
    bloom_filter_t filter;
    hashmap_t block_contained;

} native_func_manager_t;

typedef struct rujit_t {
    VALUE self;
    rb_thread_t *main_thread;
    jit_event_t *current_event;
    struct rujit_backend_t *backend;
    lir_builder_t builder;
    memory_pool_t mpool;
    native_func_manager_t manager;
#if LIR_OPT_REMOVE_REDEFINED_GUARD
    // Valid traces that none optimized trace contained
    // guard (GuardMethodRedefine)
    hashmap_t traces_with_guard;
#endif
    hashmap_t traces_with_block;
    unsigned func_id;
} rujit_t;

static rujit_t *current_jit = NULL;

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
    // jit_runtime.cTime = rb_cTime;
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
    if (!disable_jit) {
	int i;
	for (i = 0; i < BOP_LAST_; i++) {
	    jit_vm_redefined_flag[i] = global_state_ptr->_ruby_vm_redefined_flag[i];
	}
    }
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
static void jit_backend_init_cgen(rujit_t *jit);
static void jit_backend_init_llvm(rujit_t *jit)
{
    assert(0 && "unreachable");
}

static rujit_t *jit_new()
{
    rujit_t *jit = (rujit_t *)malloc(sizeof(*jit));
    memset(jit, 0, sizeof(*jit));
    jit->main_thread = GET_THREAD();

    memory_pool_init(&jit->mpool);
    lir_builder_init(&jit->builder, &jit->mpool);
    jit->backend = &backend_dummy;
    if (USE_CGEN) {
	jit_backend_init_cgen(jit);
    } else if (USE_LLVM) {
	jit_backend_init_llvm(jit);
    }
    jit->backend->f_init(jit, jit->backend);
#if LIR_OPT_REMOVE_REDEFINED_GUARD
    hashmap_init(&jit->traces_with_guard, 1);
#endif
    hashmap_init(&jit->traces_with_block, 1);

    native_func_manager_init(&jit->manager);
    if (RTEST(rb_cJit)) {
	rb_cJit = rb_define_class("Jit", rb_cObject);
	rb_undef_alloc_func(rb_cJit);
	rb_undef_method(CLASS_OF(rb_cJit), "new");
	jit->self = TypedData_Wrap_Struct(rb_cJit, &jit_data_type, jit);
	rb_gc_register_mark_object(jit->self);
    }
    jit->func_id = 0;
    return jit;
}

static void jit_delete(rujit_t *jit)
{
    jit_profile_dump();
    native_func_manager_dispose(&jit->manager);
#if LIR_OPT_REMOVE_REDEFINED_GUARD
    hashmap_dispose(&jit->traces_with_guard, NULL);
#endif
    hashmap_dispose(&jit->traces_with_block, NULL);
    jit->backend->f_delete(jit, jit->backend);
    free(jit);
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
    trace->failed_counter = 0;
    trace->native_func = NULL;
    trace->lir_func = NULL;
    trace->parent = parent;
    jit_list_init(&trace->exit_handlers);
    native_func_manager_add_trace(&jit->manager, trace);
    return trace;
}

static void jit_trace_unload_native_func(jit_trace_t *trace);
static void jit_trace_delete(jit_trace_t *trace);

static void trace_unlink(jit_trace_t *trace)
{
    jit_trace_t *buf[32] = {};
    jit_trace_t *root_trace;
    int i, bufpos = 0;

    native_func_manager_t *mng = &current_jit->manager;
    root_trace = native_func_manager_find_trace(mng, trace->start_pc);
    buf[bufpos++] = trace;

    while (bufpos != 0) {
	jit_trace_t *t = buf[--bufpos];
	for (i = 0; i < jit_list_size(&t->exit_handlers); i++) {
	    trace_side_exit_handler_t *hdl;
	    jit_trace_t *child;
	    hdl = JIT_LIST_GET(trace_side_exit_handler_t *, &t->exit_handlers, i);
	    child = hdl->child_trace;
	    child->parent = NULL;
	    buf[bufpos++] = child;
	}
	if (root_trace == t) {
	    fprintf(stderr, "drop trace from root trace set (trace=%p)", t);
	    native_func_manager_remove_trace(mng, trace->start_pc);
	}
	fprintf(stderr, "drop trace. (trace=%p)", trace);
	jit_trace_delete(trace);
    }
}

static void jit_trace_delete(jit_trace_t *trace)
{
    rujit_t *jit = current_jit;
    native_func_manager_t *mng = &jit->manager;
    if (trace->native_func) {
	int i;
	trace->counter = 0;
	jit_trace_unload_native_func(trace);
	for (i = 0; i < jit_list_size(&trace->exit_handlers); i++) {
	    trace_side_exit_handler_t *hdl;
	    hdl = JIT_LIST_GET(trace_side_exit_handler_t *, &trace->exit_handlers, i);
	    jit_trace_delete(hdl->this_trace);
	}
	native_func_manager_remove_trace(mng, trace->start_pc);
	// if (trace->side_exit) {
	//     trace->side_exit = NULL;
	// }
    }
}

static void jit_trace_unload_native_func(jit_trace_t *trace)
{
    if (trace->native_func) {
	native_func_invalidate(trace->native_func);
	trace->native_func = NULL;
    }
}

enum trace_invalid_type {
    TRACE_INVALID_TYPE_METHOD = 0,
    TRACE_INVALID_TYPE_BLOCK = 1
};

static void jit_add_valid_trace(enum trace_invalid_type type, VALUE val, jit_trace_t *trace)
{
    rujit_t *jit = current_jit;
    hashmap_t *valid_code = NULL;
    jit_list_t *list = NULL;
    if (type == TRACE_INVALID_TYPE_METHOD) {
	valid_code = &jit->traces_with_guard;
    } else {
	valid_code = &jit->traces_with_block;
    }
    if ((list = (jit_list_t *)hashmap_get(valid_code, (hashmap_data_t)val))) {
    } else {
	list = (jit_list_t *)malloc(sizeof(jit_list_t));
	jit_list_init(list);
    }
    JIT_LIST_ADD(list, trace);
    hashmap_set(valid_code, (hashmap_data_t)val, (hashmap_data_t)list);
}

static void jit_remove_invalid_trace(enum trace_invalid_type type, VALUE val)
{
    rujit_t *jit = current_jit;
    hashmap_t *valid_code = NULL;
    jit_list_t *list;
    if (type == TRACE_INVALID_TYPE_METHOD) {
	valid_code = &jit->traces_with_guard;
    } else {
	valid_code = &jit->traces_with_block;
    }
    list = (jit_list_t *)hashmap_remove(valid_code, (uintptr_t)val, NULL);
    if (list) {
	int i;
	for (i = 0; i < jit_list_size(list); i++) {
	    jit_trace_t *trace = JIT_LIST_GET(jit_trace_t *, list, i);
	    jit_trace_unload_native_func(trace);
	}
	jit_list_delete(list);
	free(list);
    }
}

void rb_jit_notify_proc_freed(void *ptr)
{
    rujit_t *jit = current_jit;
    if (jit) {
	jit_remove_invalid_trace(TRACE_INVALID_TYPE_BLOCK, (VALUE)ptr);
    }
}

static int trace_is_compiled(jit_trace_t *trace)
{
    return trace && trace->native_func != NULL;
}

static void mark_trace_header(trace_side_exit_handler_t *handler, rb_thread_t *th)
{
    rb_control_frame_t *reg_cfp = th->cfp;
    rujit_t *jit = current_jit;
    jit_trace_t *trace = NULL;
    jit_event_t ebuf, *e;
    e = jit_event_init(&ebuf, jit, th, reg_cfp, handler->exit_pc);
    if (handler->child_trace) {
	trace = handler->child_trace;
    } else /*if ((trace = jit_find_trace(jit, e)) == NULL) */ {
	trace = jit_alloc_trace(jit, e, handler->this_trace);
    }
    handler->child_trace = trace;
    if (trace->failed_counter > BLACKLIST_TRACE_THRESHOLD) {
	TODO("");
	// add_blacklist_trace(jit, trace);
    } else if (!trace_is_compiled(trace) && trace->counter++ > HOT_TRACE_THRESHOLD) {
	lir_builder_reset(&jit->builder, trace);
	start_recording();
    }
}

static int trace_invoke(rujit_t *jit, jit_event_t *e, jit_trace_t *trace)
{
    native_func_t *nfunc;
    trace_side_exit_handler_t *hdl;
    rb_thread_t *th = e->th;
    rb_control_frame_t *cfp;
L_head:
    nfunc = trace->native_func;
    cfp = th->cfp;
    JIT_PROFILE_COUNT(invoke_trace_invoke_enter);
    RC_INC(nfunc);
    hdl = nfunc->code(th, cfp);
#ifdef ENABLE_PROFILE_TRACE_JIT
    nfunc->invoked++;
#endif
    RC_DEC(nfunc);
    if (RC_CHECK(nfunc) && nfunc->code != NULL) {
	VALUE *pc = hdl->exit_pc;
	native_func_delete(nfunc);
	trace->native_func = NULL;
	th->cfp->pc = pc;
	return 1;
    }
    if (JIT_DEBUG_VERBOSE > 0 || JIT_LOG_SIDE_EXIT > 0) {
	fprintf(stderr, "trace %s for %p is exit from %p, cfp=%p\n",
#if JIT_DEBUG_TRACE
	        (nfunc->func_name) ? nfunc->func_name : "",
#else
	        "",
#endif
	        e->pc, hdl->exit_pc, th->cfp);
    }
    switch (hdl->exit_status) {
	case TRACE_EXIT_SIDE_EXIT:
	    JIT_PROFILE_COUNT(invoke_trace_side_exit);
	    if (JIT_ENABLE_LINK_TO_CHILD_TRACE) {
		JIT_PROFILE_COUNT(invoke_trace_child1);
		if (trace_is_compiled(hdl->child_trace)) {
		    trace = hdl->child_trace;
		    goto L_head;
		}
		mark_trace_header(hdl, th);
		JIT_PROFILE_COUNT(invoke_trace_child2);
		if (trace_is_compiled(hdl->child_trace)) {
		    trace = hdl->child_trace;
		    goto L_head;
		}
	    }
	    break;
	case TRACE_EXIT_LINK_TRACE: {
	    JIT_PROFILE_COUNT(invoke_trace_link_trace);
	    if (JIT_ENABLE_LINK_TO_CHILD_TRACE) {
		jit_trace_t *child = NULL;
		if (trace_is_compiled(hdl->child_trace)) {
		    trace = hdl->child_trace;
		    goto L_head;
		}
		if ((child = jit_find_trace(jit, e)) == NULL) {
		    hdl->child_trace = child;
		}
		if (trace_is_compiled(hdl->child_trace)) {
		    trace = hdl->child_trace;
		    goto L_head;
		}
	    }
	    break;
	}
	case TRACE_EXIT_SUCCESS:
	    JIT_PROFILE_COUNT(invoke_trace_success);
	    break;
	case TRACE_EXIT_ERROR:
	    assert(0 && "unreachable");
	    break;
    }
    th->cfp->pc = hdl->exit_pc;

    return 1;
}

void Init_rawjit(struct rb_vm_global_state *global_state_ptr)
{
    jit_global_default_params_setup(global_state_ptr);
    if (!disable_jit) {
	rb_cMath = rb_singleton_class(rb_mMath);
	rb_gc_register_mark_object(rb_cMath);
	jit_runtime_init(global_state_ptr);
	current_jit = jit_new();
	rujit_init_redefined_flag();
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

static int already_recorded_on_trace(jit_event_t *e, const int find_recored_block)
{
    rujit_t *jit = current_jit;
    jit_trace_t *trace;
    lir_builder_t *builder = &jit->builder;
    if (e->pc == builder->cur_trace->start_pc) {
	if (builder->inst_size > 1) {
	    return 1;
	}
    }
    if (find_recored_block) {
	basicblock_t *bb;
	if ((bb = lir_builder_find_block(&jit->builder, e->pc))) {
	    if (basicblock_size(bb) != 0) {
		//FIXME we need to implement
		//      "False Loop Filtering" to remove false loop
		Emit_Exit(&jit->builder, e->pc);
		return 1;
	    }
	}
    } else if ((trace = jit_find_trace(jit, e)) != NULL) {
	if (trace_is_compiled(trace)) {
	    // TODO("");
	    // linked to other trace
	    jit_snapshot_t *snapshot;
	    snapshot = lir_builder_take_snapshot(&jit->builder, REG_PC);
	    snapshot->status = TRACE_EXIT_LINK_TRACE;
	    Emit_Exit(&jit->builder, e->pc);
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
    if (lir_builder_submitted_compile_queue(builder)) {
	e->reason = TRACE_ERROR_SUBMIT;
	return 1;
    }
    if (already_recorded_on_trace(e, 0 /*FIXME*/)) {
	e->reason = TRACE_ERROR_ALREADY_RECORDED;
	return 1;
    }
    if (lir_builder_is_full(builder)) {
	e->reason = TRACE_ERROR_BUFFER_FULL;
	lir_builder_take_snapshot(builder, REG_PC);
	//if (JIT_DEBUG_VERBOSE) {
	//    TRACE_LOG(e, "[file:%s line:%d] exit trace : buffer full\n");
	//}
	Emit_Exit(builder, e->pc);
	return 1;
    }
    //if (!is_tracable_call_inst(e)) {
    //    e->reason = TRACE_ERROR_NATIVE_METHOD;
    //    lir_builder_take_snapshot(builder, REG_PC, 1);
    //    return 1;
    //}
    if (is_irregular_event(e)) {
        jit_snapshot_t *snapshot;
	e->reason = TRACE_ERROR_THROW;
	snapshot = lir_builder_take_snapshot(builder, REG_PC);
	if (JIT_DEBUG_VERBOSE) {
	    fprintf(stderr, "exit trace : : throw error\n");
	}
	Emit_Exit(builder, e->pc);
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
    if (UNLIKELY(disable_jit || (jit && th != jit->main_thread))) {
	return;
    }
    assert(is_recording());
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    if (is_end_of_trace(builder, e)) {
	lir_builder_compile(jit, builder);
	stop_recording();
    } else {
	record_insn(builder, e);
    }
}

int rujit_invoke_or_make_trace(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    jit_event_t ebuf, *e;
    rujit_t *jit = current_jit;
    jit_trace_t *trace;
    if (UNLIKELY(disable_jit || (jit && th != jit->main_thread))) {
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
    if (trace->counter++ > HOT_TRACE_THRESHOLD) {
	//if (find_trace_in_blacklist(jit, trace)) {
	//    return 0;
	//}
	start_recording();
	lir_builder_reset(&jit->builder, trace);
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
#include "jit_optimize.h"
#include "jit_codegen.h"
