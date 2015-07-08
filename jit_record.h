/**********************************************************************

  record.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

void vm_search_super_method(rb_thread_t *th, rb_control_frame_t *reg_cfp, rb_call_info_t *ci);

#undef GET_GLOBAL_CONSTANT_STATE
#define GET_GLOBAL_CONSTANT_STATE() (*jit_runtime.global_constant_state)

#define _POP() lir_builder_pop(builder)
#define _PUSH(REG) lir_builder_push(builder, REG)
#define _TOPN(N) lir_builder_topn(builder, (int)(N))
#define _SET(N, REG) lir_builder_set(builder, (int)(N), REG)
#define _SWAP(T, A, B) \
    do {               \
	T tmp = (A);   \
	A = B;         \
	B = tmp;       \
    } while (0)
#define LIR_SWAP(A, B) _SWAP(lir_t, A, B)
#define VAL_SWAP(A, B) _SWAP(VALUE, A, B)
#define IS_Fixnum(V) FIXNUM_P(V)
#define IS_Float(V) FLONUM_P(V)
#define IS_String(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cString))
#define IS_Array(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cArray))
#define IS_Hash(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cHash))
#define IS_Regexp(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cRegexp))
#define IS_Math(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cMath))
#define IS_Object(V) (!SPECIAL_CONST_P(V) && (RB_TYPE_P(V, T_OBJECT)))
#define IS_NonSpecialConst(V) (!SPECIAL_CONST_P(V))
#define CURRENT_PC (e->pc)

#define JIT_OP_UNREDEFINED_P(op, klass) (LIKELY((JIT_RUNTIME->redefined_flag[(op)] & (klass)) == 0))

#define not_support_op(builder, E, OPNAME)                               \
    do {                                                                 \
	static const char msg[] = "not support bytecode: " OPNAME;       \
	trace_recorder_abort(builder, e, TRACE_ERROR_UNSUPPORT_OP, msg); \
	return;                                                          \
    } while (0)

#define take_snapshot(builder, pc) lir_builder_take_snapshot(builder, pc)

#define IS_ATTR 1
#define IS_NOT_ATTR 0

/* util */
static lir_t emit_envload(lir_builder_t *builder, int lev, int idx)
{
    basicblock_t *bb = lir_builder_cur_bb(builder);
    lir_t Rval = EmitIR(EnvLoad, lev, idx);
    lir_builder_set_localvar(builder, bb, lev, idx, Rval);
    return Rval;
}

static lir_t emit_envstore(lir_builder_t *builder, int lev, int idx, lir_t val)
{
    basicblock_t *bb = lir_builder_cur_bb(builder);
    lir_t Rval = EmitIR(EnvStore, lev, idx, val);
    lir_builder_set_localvar(builder, bb, lev, idx, Rval);
    return Rval;
}

static lir_t emit_load_const(lir_builder_t *builder, VALUE val)
{
    unsigned inst_size;
    basicblock_t *entry_bb = lir_builder_entry_bb(builder);
    basicblock_t *cur_bb = lir_builder_cur_bb(builder);
    lir_t Rval = lir_builder_get_const(builder, val);
    if (Rval) {
	return Rval;
    }
    lir_builder_set_bb(builder, entry_bb);
    inst_size = basicblock_size(entry_bb);
    if (NIL_P(val)) {
	Rval = EmitIR(LoadConstNil);
    } else if (val == Qtrue || val == Qfalse) {
	Rval = EmitIR(LoadConstBoolean, val);
    } else if (FIXNUM_P(val)) {
	Rval = EmitIR(LoadConstFixnum, val);
    } else if (FLONUM_P(val)) {
	Rval = EmitIR(LoadConstFloat, val);
    } else if (!SPECIAL_CONST_P(val)) {
	if (RBASIC_CLASS(val) == rb_cString) {
	    Rval = EmitIR(LoadConstString, val);
	} else if (RBASIC_CLASS(val) == rb_cRegexp) {
	    Rval = EmitIR(LoadConstRegexp, val);
	}
    }

    if (Rval == NULL) {
	Rval = EmitIR(LoadConstObject, val);
    }
    lir_builder_add_const(builder, val, Rval);
    if (inst_size > 0 && inst_size != basicblock_size(entry_bb)) {
	basicblock_swap_inst(entry_bb, inst_size - 1, inst_size);
    }
    lir_builder_set_bb(builder, cur_bb);
    return Rval;
}

#include "jit_args.h"

static int emit_jump(lir_builder_t *builder, VALUE *pc, int find_block);

static CALL_INFO lir_builder_clone_cache(lir_builder_t *builder, CALL_INFO ci)
{
    CALL_INFO newci = (CALL_INFO)malloc(sizeof(*newci));
    memcpy(newci, ci, sizeof(*newci));
    JIT_LIST_ADD(&builder->call_caches, newci);
    return newci;
}

static inline int
simple_iseq_p(const rb_iseq_t *iseq)
{
    return iseq->param.flags.has_opt == FALSE && iseq->param.flags.has_rest == FALSE && iseq->param.flags.has_post == FALSE && iseq->param.flags.has_kw == FALSE && iseq->param.flags.has_kwrest == FALSE && iseq->param.flags.has_block == FALSE;
}

static inline VALUE
vm_callee_setup_block_arg_arg0_check(VALUE *argv)
{
    VALUE ary, arg0 = argv[0];
    ary = rb_check_array_type(arg0);
    argv[0] = arg0;
    return ary;
}

static inline int
vm_callee_setup_block_arg_arg0_splat(rb_control_frame_t *cfp, const rb_iseq_t *iseq, VALUE *argv, VALUE ary)
{
    int i;
    long len = RARRAY_LEN(ary);

    // CHECK_VM_STACK_OVERFLOW(cfp, iseq->param.lead_num);

    for (i = 0; i < len && i < iseq->param.lead_num; i++) {
	argv[i] = RARRAY_AREF(ary, i);
    }

    return i;
}

static int lir_builder_callee_setup_arg(lir_builder_t *builder, CALL_INFO ci, lir_t *regs,
                                        const rb_iseq_t *iseq, VALUE *argv, lir_t *Rargv, lir_t *Rkw_hash,
                                        enum arg_setup_type arg_setup_type, jit_snapshot_t *snapshot)
{
    jit_event_t *e = current_jit->current_event;
    rb_thread_t *th = e->th;
    if (LIKELY(simple_iseq_p(iseq))) {
	rb_control_frame_t *cfp = th->cfp;
	VALUE arg0;

	if (UNLIKELY(IS_ARGS_SPLAT(ci)))
	    vm_caller_setup_arg_splat(builder, cfp, ci);

	if (arg_setup_type == arg_setup_block && ci->argc == 1 && iseq->param.flags.has_lead && !iseq->param.flags.ambiguous_param0 && !NIL_P(arg0 = vm_callee_setup_block_arg_arg0_check(argv))) {
	    ci->argc = vm_callee_setup_block_arg_arg0_splat(cfp, iseq, argv, arg0);
	}

	if (ci->argc != iseq->param.lead_num) {
	    if (arg_setup_type == arg_setup_block) {
		if (ci->argc < iseq->param.lead_num) {
		    // int i;
		    // CHECK_VM_STACK_OVERFLOW(cfp, iseq->param.lead_num);
		    //for (i = ci->argc; i < iseq->param.lead_num; i++)
		    //    argv[i] = Qnil;
		    ci->argc = iseq->param.lead_num; /* fill rest parameters */
		} else if (ci->argc > iseq->param.lead_num) {
		    ci->argc = iseq->param.lead_num; /* simply truncate arguments */
		}
	    } else if (arg_setup_type == arg_setup_lambda && ci->argc == 1 && !NIL_P(arg0 = vm_callee_setup_block_arg_arg0_check(argv)) && RARRAY_LEN(arg0) == iseq->param.lead_num) {
		ci->argc = vm_callee_setup_block_arg_arg0_splat(cfp, iseq, argv, arg0);
	    } else {
		argument_arity_error(th, iseq, ci->argc, iseq->param.lead_num, iseq->param.lead_num);
	    }
	}

	ci->aux.opt_pc = 0;
    } else {
	ci->aux.opt_pc = setup_parameters_complex(builder, th, iseq, ci, argv, arg_setup_method, snapshot);
    }
    return 0;
}

static void emit_new_instance(lir_builder_t *builder, jit_event_t *e, CALL_INFO ci, jit_snapshot_t *snapshot, int reg_argc, lir_t *reg_argv)
{
    VALUE klass = TOPN(ci->orig_argc);
    lir_t Rklass, Robj, ret;
    int i, arg_size = ci->argc + 1 + 1 /*rest*/ + 1 /*kw_hash*/;
    lir_t regs[arg_size];
    CALL_INFO old_ci = ci;
    struct RBasicRaw tmp = {};
    if ((ci->flag & VM_CALL_ARGS_BLOCKARG) || ci->blockiseq != 0) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_UNSUPPORT_OP,
	                  "Class.new with block is not supported\n");
	return;
    }
    if (UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT)) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_UNSUPPORT_OP,
	                  "Class.new with splat is not supported\n");
	return;
    }

    // find recv.class.initialize()
    if (reg_argc == -1) {
	for (i = 0; i < ci->argc + 1; i++) {
	    regs[ci->argc - i] = _POP();
	}
    } else {
	for (i = 0; i < reg_argc; i++) {
	    regs[i] = reg_argv[i];
	    _POP();
	}
    }
    Rklass = regs[0];
    tmp.klass = klass;
    ci = lir_builder_clone_cache(builder, ci);
    // ci->argc = ci->orig_argc = argc;
    ci->mid = idInitialize;
    vm_search_method(ci, (VALUE)&tmp);
    ci->flag = 0;

    for (i = 0; i < ci->argc + 1; i++) {
	_PUSH(regs[ci->argc - i]);
    }
    EmitIR(GuardClassMethod, REG_PC, Rklass, ci);
    // dump_lir_builder(builder);
    // fprintf(stderr, "%d\n", (ci->flag & VM_CALL_ARGS_SPLAT));
    // fprintf(stderr, "%d\n", (old_ci->flag & VM_CALL_ARGS_SPLAT));
    // asm volatile("int3");
    for (i = 0; i < ci->argc + 1; i++) {
	_POP();
    }

    // user defined ruby method
    if (ci->me && ci->me->def->type == VM_METHOD_TYPE_ISEQ) {
	lir_func_t *newfunc;
	Robj = EmitIR(AllocObject, REG_PC, ci, Rklass);
	emit_jump(builder, REG_PC, 0);
	newfunc = lir_func_new2(builder->mpool, REG_PC, 1);
	ret = EmitIR(InvokeConstructor, newfunc, ci, Robj, ci->argc, regs);
	lir_builder_push_call_frame(builder, newfunc, ret);
	JIT_LIST_ADD(&builder->func_list, newfunc);
    } else {
	_PUSH(EmitIR(CallMethod, old_ci, ci->argc + 1, regs));
    }
}

static void emit_call_method(lir_builder_t *builder, CALL_INFO ci, jit_snapshot_t *snapshot, int reg_argc, lir_t *reg_argv)
{
    jit_event_t *e = current_jit->current_event;
    int i, arg_size = ci->argc + 1 + 1 /*rest*/ + 1 /*kw_hash*/;
    lir_t regs[arg_size];
    int invokesuper = e->opcode == BIN(invokesuper);
    lir_t Rblock = NULL;
    rb_block_t *block = NULL;
    // int has_block = UNLIKELY(ci->flag & VM_CALL_ARGS_BLOCKARG);

    memset(regs, 0, sizeof(lir_t) * arg_size);
    ci->argc = ci->orig_argc;

    if (lir_builder_too_many_call_frame(builder)) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_CALL_FRAME_FULL,
	                  "invoke too many method or block");
	return;
    }

    if (invokesuper) {
	ci->recv = GET_SELF();
	vm_search_super_method(e->th, GET_CFP(), ci);
    } else {
	vm_search_method(ci, ci->recv = TOPN(ci->orig_argc));
    }

    if (reg_argc != -1) {
	for (i = 0; i < reg_argc; i++) {
	    regs[i] = reg_argv[i];
	    // _PUSH(regs[i]);
	}
    }
    // user defined ruby method
    if (ci->me && ci->me->def->type == VM_METHOD_TYPE_ISEQ) {
	// emit_invoke_user_defined_method(builder, ci, snapshot, argc, argv);
	lir_func_t *newfunc = NULL;
	int argc = ci->argc;
	ISEQ iseq = ci->me->def->body.iseq;
	lir_t ret = NULL;
	lir_t Rargv = NULL;
	lir_t Rkw_hash = NULL;
	lir_t Rself = NULL;

	if (UNLIKELY(ci->flag & VM_CALL_ARGS_BLOCKARG)) {
	    TODO("send VM_CALL_ARGS_BLOCKARG");
	    return;
	} else if (ci->blockiseq != 0) {
	    ci->blockptr = RUBY_VM_GET_BLOCK_PTR_IN_CFP(REG_CFP);
	    ci->blockptr->iseq = ci->blockiseq;
	    ci->blockptr->proc = 0;
	    Rblock = EmitIR(LoadSelfAsBlock, ci->blockiseq);
	    block = ci->blockptr;
	}

	/* block arguments */
	if (iseq->param.flags.has_block == TRUE) {
	    const rb_block_t *blockptr = ci->blockptr;
	    if (blockptr) {
		if (blockptr->proc == 0) {
		    Rblock = EmitIR(AllocBlock, Rblock);
		}
	    }
	}

	if (Rblock) {
	    EmitIR(GuardBlockEqual, REG_PC, Rblock, block->iseq);
	}

	ci = lir_builder_clone_cache(builder, ci);
	if (invokesuper) {
	    lir_t Rclass;
	    Rself = EmitIR(LoadSelf);
	    Rclass = EmitIR(ObjectClass, Rself);
	    EmitIR(GuardClassMethod, REG_PC, Rclass, ci);
	} else {
	    if (reg_argc == -1) {
		regs[0] = _TOPN(ci->orig_argc);
	    }
	    EmitIR(GuardMethodCache, REG_PC, regs[0], ci);
	}
	if (invokesuper) {
	    // original regs[0] is dummy reciver
	    // @see compile.c(iseq_compile_each)
	    regs[0] = Rself;
	}

	for (i = 0; i < argc + 1; i++) {
	    regs[argc - i] = _POP();
	}

	if (lir_builder_callee_setup_arg(builder, ci, regs, iseq, REG_CFP->sp - argc,
	                                 &Rargv, &Rkw_hash,
	                                 arg_setup_method, snapshot)) {
	    assert(is_recording() == 0); // tracer is already stoppend;
	    return;
	}

	argc = ci->argc;
	if (Rargv != NULL) {
	    IAllocArray *ir = (IAllocArray *)Rargv;
	    argc += 1 - ir->argc;
	    regs[argc] = Rargv;
	}
	newfunc = lir_func_new2(builder->mpool, REG_PC, 0);
	lir_func_add_args(newfunc, argc + 1, regs);
	if (Rblock) {
	    lir_func_add_block_arg(newfunc, Rblock);
	}
	// for (i = 0; i < argc + 1; i++) {
	//    _PUSH(regs[i]);
	// }
	// lir_builder_remove_stack(builder, argc + 1);
	//for (i = 0; i < argc + 1; i++) {
	//   _POP();
	//}
	emit_jump(builder, REG_PC, 0);
	ret = EmitIR(InvokeMethod, newfunc, ci, Rblock, argc + 1, regs);
	lir_builder_push_call_frame(builder, newfunc, ret);
	JIT_LIST_ADD(&builder->func_list, newfunc);
	return;
    } else if (ci->me && ci->me->def->type == VM_METHOD_TYPE_OPTIMIZED) {
	switch (ci->me->def->body.optimize_type) {
	    case OPTIMIZED_METHOD_TYPE_SEND:
		//ret = send_internal(ci->argc, argv, ci->recv, CALL_FCALL);
		break;
	    case OPTIMIZED_METHOD_TYPE_CALL:
		//{
		//    rb_proc_t *proc;
		//    GetProcPtr(ci->recv, proc);
		//    ret = rb_vm_invoke_proc(th, proc, ci->argc, argv, ci->blockptr);
		//    break;
		//}
		break;
	    default:
		rb_bug("emit_call_method: unsupported optimized method type (%d)", ci->me->def->body.optimize_type);
	}
    } else {
	// check ClassA.new(argc, argv)
	if (check_cfunc(ci->me, rb_class_new_instance)) {
	    if (ci->me->klass == rb_cClass) {
		emit_new_instance(builder, e, ci, snapshot, reg_argc, regs);
		return;
	    }
	}

	// check block_given?
	if (check_cfunc(ci->me, rb_f_block_given_p) && ci->argc == 0) {
	    lir_t Rrecv = regs[0];
	    ci = lir_builder_clone_cache(builder, ci);
	    EmitIR(GuardMethodCache, REG_PC, Rrecv, ci);
	    _PUSH(EmitIR(InvokeNative, ci, rb_f_block_given_p, 0, NULL));
	    return;
	}

	if (RCLASS_SUPER(CLASS_OF(ci->recv)) == rb_cStruct) {
	    lir_t Rrecv = regs[0];
	    // if `recv` is subclass of `Struct` and method name is not `new`
	    // we call a method directly
	    if (ci->mid != rb_intern("new")) {
		if (ci->me && ci->me->def->type == VM_METHOD_TYPE_CFUNC) {
		    EmitIR(GuardMethodCache, REG_PC, Rrecv, ci);
		    _PUSH(EmitIR(InvokeNative, ci, ci->me->def->body.cfunc.func, ci->argc + 1, regs));
		    return;
		}
	    }
	}
    }

    lir_builder_abort(builder, snapshot, TRACE_ERROR_NATIVE_METHOD, "invoking native method");
}

static void emit_call_method0(lir_builder_t *builder, CALL_INFO ci, jit_snapshot_t *snapshot)
{
    emit_call_method(builder, ci, snapshot, -1, NULL);
}

static void emit_call_method1(lir_builder_t *builder, CALL_INFO ci, jit_snapshot_t *snapshot, lir_t recv)
{
    lir_t args[1];
    args[0] = recv;
    emit_call_method(builder, ci, snapshot, 1, args);
}

static void emit_call_method2(lir_builder_t *builder, CALL_INFO ci, jit_snapshot_t *snapshot, lir_t recv, lir_t obj)
{
    lir_t args[2];
    args[0] = recv;
    args[1] = obj;
    emit_call_method(builder, ci, snapshot, 2, args);
}

static void emit_call_method3(lir_builder_t *builder, CALL_INFO ci, jit_snapshot_t *snapshot, lir_t recv, lir_t idx, lir_t obj)
{
    lir_t args[3];
    args[0] = recv;
    args[1] = idx;
    args[2] = obj;
    emit_call_method(builder, ci, snapshot, 3, args);
}

static lir_t emit_get_prop(lir_builder_t *builder, CALL_INFO ci, lir_t Rrecv, jit_snapshot_t *snapshot)
{
    jit_event_t *e = current_jit->current_event;
    VALUE obj = ci->recv;
    ID id = ci->me->def->body.attr.id;
    int cacheable = vm_load_cache(obj, id, 0, ci, 1);
    int index = ci->aux.index - 1;
    assert(index >= 0 && cacheable);
    EmitIR(GuardProperty, e->pc, Rrecv, IS_ATTR, id, index, ci->class_serial);
    return EmitIR(GetPropertyName, Rrecv, index);
}

static lir_t emit_set_prop(lir_builder_t *builder, CALL_INFO ci, lir_t Rrecv, lir_t Rval, jit_snapshot_t *snapshot)
{
    jit_event_t *e = current_jit->current_event;
    VALUE obj = ci->recv;
    ID id = ci->me->def->body.attr.id;
    int cacheable = vm_load_or_insert_ivar(obj, id, Qnil /*FIXME*/, NULL, ci, 1);
    int index = ci->aux.index - 1;
    assert(index >= 0 && cacheable);
    EmitIR(GuardProperty, REG_PC, Rrecv, IS_ATTR, id, index, ci->class_serial);
    if (cacheable == 1) {
	Rval = EmitIR(SetPropertyName, Rrecv, 0, index, Rval);
    } else {
	Rval = EmitIR(SetPropertyName, Rrecv, (long)id, index, Rval);
    }
    _PUSH(Rval);
    return Rval;
}

#include "yarv2lir.c"

static void record_getspecial(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setspecial(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_getinstancevariable(lir_builder_t *builder, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    ID id = (ID)GET_OPERAND(1);
    VALUE obj = GET_SELF();
    lir_t Rrecv;
    int cacheable;

    take_snapshot(builder, REG_PC);
    Rrecv = EmitIR(LoadSelf);
    cacheable = vm_load_cache(obj, id, ic, NULL, 0);
    if (cacheable == 1) {
	size_t index = ic->ic_value.index;
	EmitIR(GuardTypeObject, REG_PC, Rrecv);
	EmitIR(GuardProperty, REG_PC, Rrecv, IS_NOT_ATTR, id, index, ic->ic_serial);
	_PUSH(EmitIR(GetPropertyName, Rrecv, index));
    } else {
	TODO("getinstancevariable");
    }
}

static void record_setinstancevariable(lir_builder_t *builder, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    ID id = (ID)GET_OPERAND(1);
    VALUE val = TOPN(0);
    VALUE obj = GET_SELF();
    lir_t Rrecv;
    int cacheable;

    take_snapshot(builder, REG_PC);
    Rrecv = EmitIR(LoadSelf);
    cacheable = vm_load_or_insert_ivar(obj, id, val, ic, NULL, 0);
    if (cacheable) {
	lir_t Rval;
	size_t index = ic->ic_value.index;
	EmitIR(GuardTypeObject, REG_PC, Rrecv);
	EmitIR(GuardProperty, REG_PC, Rrecv, IS_NOT_ATTR, id, index, ic->ic_serial);
	Rval = _POP();
	if (cacheable == 1) {
	    EmitIR(SetPropertyName, Rrecv, 0, index, Rval);
	} else {
	    EmitIR(SetPropertyName, Rrecv, (long)id, index, Rval);
	}
    } else {
	TODO("setinstancevariable");
    }
}

static void record_getclassvariable(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setclassvariable(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_getconstant(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setconstant(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

/* copied from vm_insnhelper.c */
extern NODE *rb_vm_get_cref(const rb_iseq_t *iseq, const VALUE *ep);
static inline VALUE jit_vm_get_cbase(const rb_iseq_t *iseq, const VALUE *ep)
{
    NODE *cref = rb_vm_get_cref(iseq, ep);
    VALUE klass = Qundef;

    while (cref) {
	if ((klass = cref->nd_clss) != 0) {
	    break;
	}
	cref = cref->nd_next;
    }

    return klass;
}

static inline VALUE jit_vm_get_const_base(const rb_iseq_t *iseq, const VALUE *ep)
{
    NODE *cref = rb_vm_get_cref(iseq, ep);
    VALUE klass = Qundef;

    while (cref) {
	if (!(cref->flags & NODE_FL_CREF_PUSHED_BY_EVAL)
	    && (klass = cref->nd_clss) != 0) {
	    break;
	}
	cref = cref->nd_next;
    }

    return klass;
}

static void record_putspecialobject(lir_builder_t *builder, jit_event_t *e)
{
    enum vm_special_object_type type = (enum vm_special_object_type)GET_OPERAND(1);
    VALUE val = 0;
    switch (type) {
	case VM_SPECIAL_OBJECT_VMCORE:
	    val = rb_mRubyVMFrozenCore;
	    break;
	case VM_SPECIAL_OBJECT_CBASE:
	    val = jit_vm_get_cbase(GET_ISEQ(), GET_EP());
	    break;
	case VM_SPECIAL_OBJECT_CONST_BASE:
	    val = jit_vm_get_const_base(GET_ISEQ(), GET_EP());
	    break;
	default:
	    rb_bug("putspecialobject insn: unknown value_type");
    }
    _PUSH(EmitIR(LoadConstSpecialObject, val));
}

static void record_concatstrings(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t num = (rb_num_t)GET_OPERAND(1);
    rb_num_t i = num - 1;

    lir_t Rval = EmitIR(AllocString, _TOPN(i));
    while (i-- > 0) {
	Rval = EmitIR(StringConcat, Rval, _TOPN(i));
    }
    for (i = 0; i < num; i++) {
	_POP();
    }
    _PUSH(Rval);
}

static void record_toregexp(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t i;
    rb_num_t cnt = (rb_num_t)GET_OPERAND(2);
    rb_num_t opt = (rb_num_t)GET_OPERAND(1);
    lir_t regs[cnt];
    lir_t Rary;
    for (i = 0; i < cnt; i++) {
	regs[cnt - i - 1] = _POP();
    }
    Rary = EmitIR(AllocArray, (int)cnt, regs);
    _PUSH(EmitIR(AllocRegexFromArray, Rary, (int)opt));
}

static void record_newarray(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    lir_t argv[num];
    for (i = 0; i < num; i++) {
	argv[i] = _POP();
    }
    _PUSH(EmitIR(AllocArray, (int)num, argv));
}

static void record_duparray(lir_builder_t *builder, jit_event_t *e)
{
    VALUE val = (VALUE)GET_OPERAND(1);
    lir_t Rval = emit_load_const(builder, val);
    _PUSH(EmitIR(ArrayDup, Rval));
}

static void record_expandarray(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t num = (rb_num_t)GET_OPERAND(1);
    rb_num_t flag = (rb_num_t)GET_OPERAND(2);
    VALUE ary = TOPN(0);

    int is_splat = flag & 0x01;
    rb_num_t space_size = num + is_splat;
    lir_t Rnil = NULL;
    lir_t Rary = NULL;
    rb_num_t len = RARRAY_LEN(ary);
    lir_t regs[len];

    if (!RB_TYPE_P(ary, T_ARRAY)) {
	TODO("expandarray");
	// not_support_op(builder, e, "expandarray");
	return;
    }
    Rary = _TOPN(0);
    take_snapshot(builder, REG_PC);
    EmitIR(GuardTypeArray, REG_PC, Rary);
    EmitIR(GuardArraySize, REG_PC, Rary, len);
    _POP(); // Rary

    Rnil = emit_load_const(builder, Qnil);
    if (flag & 0x02) {
	/* post: ..., nil ,ary[-1], ..., ary[0..-num] # top */
	rb_num_t i = 0, j;

	if (len < num) {
	    for (i = 0; i < num - len; i++) {
		_PUSH(Rnil);
	    }
	}
	for (j = 0; i < num; i++, j++) {
	    lir_t Ridx = emit_load_const(builder, LONG2FIX(len - j - 1));
	    _PUSH(EmitIR(ArrayGet, Rary, Ridx));
	}
	if (is_splat) {
	    i = 0;
	    for (; j < len; ++j) {
		lir_t Ridx = emit_load_const(builder, LONG2FIX(i));
		regs[i] = EmitIR(ArrayGet, Rary, Ridx);
		i++;
	    }
	    _PUSH(EmitIR(AllocArray, (int)i, regs));
	}
    } else {
	/* normal: ary[num..-1], ary[num-2], ary[num-3], ..., ary[0] # top */
	rb_num_t i;
	lir_t regs2[space_size];

	for (i = 0; i < num; i++) {
	    lir_t Ridx;
	    if (len <= i) {
		for (; i < num; i++) {
		    regs2[i] = Rnil;
		}
		break;
	    }
	    Ridx = emit_load_const(builder, LONG2FIX(i));
	    regs2[i] = EmitIR(ArrayGet, Rary, Ridx);
	}
	for (i = num; i > 0; i--) {
	    _PUSH(regs2[i - 1]);
	}
	if (is_splat) {
	    if (num > len) {
		_PUSH(EmitIR(AllocArray, 0, regs));
	    } else {
		for (i = 0; i < len - num; ++i) {
		    lir_t Ridx = emit_load_const(builder, LONG2FIX(i + num));
		    regs[i] = EmitIR(ArrayGet, Rary, Ridx);
		}
		_PUSH(EmitIR(AllocArray, (int)(len - num), regs));
	    }
	}
    }
    RB_GC_GUARD(ary);
}

static void record_concatarray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_splatarray(lir_builder_t *builder, jit_event_t *e)
{
    VALUE flag = (VALUE)GET_OPERAND(1);
    VALUE ary = TOPN(0);
    VALUE tmp;
    lir_t Rary = _TOPN(0);
    jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
    tmp = rb_check_convert_type(ary, T_ARRAY, "Array", "to_a");
    if (NIL_P(tmp)) {
	// FIXME
	lir_builder_abort(builder, snapshot, TRACE_ERROR_UNSUPPORT_OP,
	                  "splatarray with non-ary\n");
	// tmp = rb_ary_new3(1, ary);
	return;
    } else {
	EmitIR(GuardTypeArray, REG_PC, Rary);
    }
    _POP();
    if (RTEST(flag)) {
	Rary = EmitIR(ArrayDup, Rary);
    }
    _PUSH(Rary);
}

static void record_newhash(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    lir_t argv[num];
    for (i = num; i > 0; i -= 2) {
	argv[i - 1] = _POP(); // key
	argv[i - 2] = _POP(); // val
    }
    _PUSH(EmitIR(AllocHash, (int)num, argv));
}

static void record_newrange(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t flag = (rb_num_t)GET_OPERAND(1);
    lir_t Rhigh = _POP();
    lir_t Rlow = _POP();
    _PUSH(EmitIR(AllocRange, Rlow, Rhigh, (int)flag));
}

static void record_dupn(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t i, n = (rb_num_t)GET_OPERAND(1);
    lir_t argv[n];
    // FIXME optimize
    for (i = 0; i < n; i++) {
	argv[i] = _TOPN(n - i - 1);
    }
    for (i = 0; i < n; i++) {
	_PUSH(argv[i]);
    }
}

static void record_topn(lir_builder_t *builder, jit_event_t *e)
{
    lir_t Rval;
    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    assert(0 && "need to test");
    Rval = _TOPN(n);
    _PUSH(Rval);
}

static void record_setn(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    lir_t Rval = _TOPN(0);
    _SET(n, Rval);
}

static void record_adjuststack(lir_builder_t *builder, jit_event_t *e)
{
    rb_num_t i, n = (rb_num_t)GET_OPERAND(1);
    for (i = 0; i < n; i++) {
	_POP();
    }
}

static void record_defined(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_checkmatch(lir_builder_t *builder, jit_event_t *e)
{
    lir_t Rpattern = _POP();
    lir_t Rtarget = _POP();
    rb_event_flag_t flag = (rb_event_flag_t)GET_OPERAND(1);
    enum vm_check_match_type checkmatch_type
        = (enum vm_check_match_type)(flag & VM_CHECKMATCH_TYPE_MASK);
    if (flag & VM_CHECKMATCH_ARRAY) {
	_PUSH(EmitIR(PatternMatchRange, Rpattern, Rtarget, checkmatch_type));
    } else {
	_PUSH(EmitIR(PatternMatch, Rpattern, Rtarget, checkmatch_type));
    }
}

static void record_checkkeyword(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_defineclass(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_send(lir_builder_t *builder, jit_event_t *e)
{
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
    emit_call_method(builder, ci, snapshot, -1, NULL);
}

static void record_invokesuper(lir_builder_t *builder, jit_event_t *e)
{
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
    emit_call_method(builder, ci, snapshot, -1, NULL);
}

static void record_invokeblock(lir_builder_t *builder, jit_event_t *e)
{
    const rb_block_t *block;
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    int i, argc = 1 /*recv*/ + ci->orig_argc;
    int arg_size = ci->argc + 1 + 1 /*rest*/;
    lir_t Rblock, ret;
    lir_t regs[arg_size];
    lir_func_t *newfunc = NULL;
    VALUE type;

    jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
    if (lir_builder_too_many_call_frame(builder)) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_CALL_FRAME_FULL,
	                  "invoke too many method or block");
	return;
    }
    block = rb_vm_control_frame_block_ptr(REG_CFP);

    ci->argc = ci->orig_argc;
    ci->blockptr = 0;
    ci->recv = GET_SELF();

    type = GET_ISEQ()->local_iseq->type;

    if ((type != ISEQ_TYPE_METHOD && type != ISEQ_TYPE_CLASS) || block == 0) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_UNSUPPORT_OP,
	                  "no block given (yield)");
	return;
    }

    if (UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT)) {
	TODO("not supported: VM_CALL_ARGS_SPLAT");
	return;
    }

    if (BUILTIN_TYPE(block->iseq) == T_NODE) {
	lir_builder_abort(builder, snapshot, TRACE_ERROR_NATIVE_METHOD,
	                  "yield native block");
	return;
    }

    regs[0] = EmitIR(LoadSelf);
    Rblock = EmitIR(LoadBlock);
    EmitIR(GuardBlockEqual, REG_PC, Rblock, block->iseq);
    if (block && block->proc) {
	rb_proc_t *proc;
	GetProcPtr(block->proc, proc);
	jit_add_valid_trace(TRACE_INVALID_TYPE_BLOCK, (VALUE)proc,
	                    builder->cur_trace);
    }
    for (i = 0; i < ci->orig_argc; i++) {
	regs[ci->orig_argc - i] = _POP();
    }

    newfunc = lir_func_new2(builder->mpool, REG_PC, 0);
    emit_jump(builder, REG_PC, 0);
    ret = EmitIR(InvokeBlock, newfunc, ci, Rblock, argc, regs);
    lir_builder_push_call_frame(builder, newfunc, ret);
}

static void record_leave(lir_builder_t *builder, jit_event_t *e)
{
    lir_func_t *func;
    lir_t val;
    if (jit_list_size(&builder->call_frame) == 0) {
	jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
	//TODO
	lir_builder_abort(builder, snapshot, TRACE_ERROR_NATIVE_METHOD,
	                  "return to untraced method");
	return;
    }
    val = _POP();
    func = lir_builder_pop_call_frame(builder);
    if (lir_func_is_constructor(func)) {
	val = EmitIR(LoadSelf);
    }
    EmitIR(FramePop);
    emit_jump(builder, REG_PC, 0);
    _PUSH(val);
}

static void record_throw(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static int emit_jump(lir_builder_t *builder, VALUE *pc, int find_block)
{
    int found = 0;
    basicblock_t *bb = NULL;
    if (find_block) {
	bb = lir_builder_find_block(builder, pc);
    }
    if (bb != NULL) {
	found = 1;
    } else {
	bb = lir_builder_create_block(builder, pc);
    }

    assert(bb != NULL);
    EmitIR(Jump, bb);
    lir_builder_set_bb(builder, bb);
    return found;
}

static void record_jump(lir_builder_t *builder, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    VALUE *jump_pc = e->pc + insn_len(BIN(branchif)) + dst;
    emit_jump(builder, jump_pc, 1);
}

static void record_branchif(lir_builder_t *builder, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    lir_t Rval = _POP();
    VALUE val = TOPN(0);
    VALUE *next_pc = e->pc + insn_len(BIN(branchif));
    VALUE *jump_pc = next_pc + dst;
    VALUE *exit_pc = NULL;
    lir_t Rguard = NULL;
    int found_bb = 0;
    jit_snapshot_t *snapshot = NULL;
    if (RTEST(val)) {
	snapshot = take_snapshot(builder, next_pc);
	Rguard = EmitIR(GuardTypeNil, next_pc, Rval);
	found_bb = emit_jump(builder, jump_pc, 1);
	exit_pc = next_pc;
    } else {
	snapshot = take_snapshot(builder, jump_pc);
	Rguard = EmitIR(GuardTypeNonNil, jump_pc, Rval);
	found_bb = emit_jump(builder, next_pc, 1);
	exit_pc = jump_pc;
    }
    // fprintf(stderr, "jump_pc=%p next_pc=%p\n", jump_pc, next_pc);
    snapshot->status = TRACE_EXIT_SIDE_EXIT;
    if (found_bb) { // it seems to formed loop
	// lir_builder_find_block(builder, jump_pc) || lir_builder_find_block(builder, next_pc)
	// it seems to formed loop
	// snapshot = take_snapshot(builder, exit_pc);
	// EmitIR(Exit, exit_pc);
	lir_builder_push_compile_queue(builder);
    }
}

static void record_branchunless(lir_builder_t *builder, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    lir_t Rval = _POP();
    VALUE val = TOPN(0);
    VALUE *next_pc = e->pc + insn_len(BIN(branchunless));
    VALUE *jump_pc = next_pc + dst;
    lir_t Rguard = NULL;
    int found_bb = 0;
    jit_snapshot_t *snapshot = NULL;
    if (!RTEST(val)) {
	snapshot = take_snapshot(builder, next_pc);
	Rguard = EmitIR(GuardTypeNonNil, next_pc, Rval);
	found_bb = emit_jump(builder, jump_pc, 1);
    } else {
	snapshot = take_snapshot(builder, jump_pc);
	Rguard = EmitIR(GuardTypeNil, jump_pc, Rval);
	found_bb = emit_jump(builder, next_pc, 1);
    }
    snapshot->status = TRACE_EXIT_SIDE_EXIT;
    //if (lir_builder_find_block(builder, jump_pc) || lir_builder_find_block(builder, next_pc)) {
    //    // it seems to formed loop
    //}
    //else {
    //    snapshot->status = TRACE_EXIT_SIDE_EXIT;
    //}
    if (found_bb) { // it seems to formed loop
	lir_builder_push_compile_queue(builder);
    }
}

static void record_getinlinecache(lir_builder_t *builder, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    if (ic->ic_serial != GET_GLOBAL_CONSTANT_STATE()) {
	// constant value is re-defined.
	TODO("getinlinecache");
	return;
    }
    _PUSH(emit_load_const(builder, ic->ic_value.value));
}

static void record_setinlinecache(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_once(lir_builder_t *builder, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    ISEQ iseq = (ISEQ)GET_OPERAND(1);

    union iseq_inline_storage_entry *is = (union iseq_inline_storage_entry *)ic;
#define RUNNING_THREAD_ONCE_DONE ((rb_thread_t *)(0x1))
    if (is->once.running_thread == RUNNING_THREAD_ONCE_DONE) {
	VALUE val = is->once.value;
	_PUSH(emit_load_const(builder, val));
    } else {
	jit_snapshot_t *snapshot = take_snapshot(builder, REG_PC);
	lir_builder_abort(builder, snapshot, TRACE_ERROR_UNSUPPORT_OP, "once\n");
	return;
    }
}

static void record_opt_case_dispatch(lir_builder_t *builder, jit_event_t *e)
{
    lir_t Rkey = _TOPN(0);
    OFFSET else_offset = (OFFSET)GET_OPERAND(2);
    CDHASH hash = (CDHASH)GET_OPERAND(1);
    VALUE key = TOPN(0);
    int type;
    st_data_t val;

    take_snapshot(builder, REG_PC);
    type = TYPE(key);
    switch (type) {
	case T_FLOAT: {
	    // FIXME
	    TODO("opt_case_dispatch");
	    //double ival;
	    //if (modf(RFLOAT_VALUE(key), &ival) == 0.0) {
	    //  key = FIXABLE(ival) ? LONG2FIX((long)ival) : rb_dbl2big(ival);
	    //}
	}
	case T_SYMBOL: /* fall through */
	case T_FIXNUM:
	case T_BIGNUM:
	case T_STRING:
	    break;
	default:
	    break;
    }
    if (BASIC_OP_UNREDEFINED_P(BOP_EQQ,
                               SYMBOL_REDEFINED_OP_FLAG | FIXNUM_REDEFINED_OP_FLAG | BIGNUM_REDEFINED_OP_FLAG | STRING_REDEFINED_OP_FLAG)) {
	if (type == T_SYMBOL) {
	    EmitIR(GuardTypeSymbol, REG_PC, Rkey);
	    EmitIR(GuardMethodRedefine, REG_PC, SYMBOL_REDEFINED_OP_FLAG, BOP_EQQ);
	} else if (type == T_FLOAT) {
	    EmitIR(GuardTypeFloat, REG_PC, Rkey);
	    EmitIR(GuardMethodRedefine, REG_PC, FLOAT_REDEFINED_OP_FLAG, BOP_EQQ);
	} else if (type == T_FIXNUM) {
	    EmitIR(GuardTypeFixnum, REG_PC, Rkey);
	    EmitIR(GuardMethodRedefine, REG_PC, FIXNUM_REDEFINED_OP_FLAG, BOP_EQQ);
	} else if (type == T_BIGNUM) {
	    EmitIR(GuardTypeBignum, REG_PC, Rkey);
	    EmitIR(GuardMethodRedefine, REG_PC, BIGNUM_REDEFINED_OP_FLAG, BOP_EQQ);
	} else if (type == T_STRING) {
	    EmitIR(GuardTypeString, REG_PC, Rkey);
	    EmitIR(GuardMethodRedefine, REG_PC, STRING_REDEFINED_OP_FLAG, BOP_EQQ);
	} else {
	    assert(0 && "unreachable");
	}
	_POP(); // pop Rkey
	// RuJIT assume `hash` is constant variable
	if (st_lookup(RHASH_TBL_RAW(hash), key, &val)) {
	    VALUE *dst = REG_PC + insn_len(BIN(opt_case_dispatch))
	                 + FIX2INT((VALUE)val);
	    emit_jump(builder, dst, 1);
	} else {
	    VALUE *dst = REG_PC + insn_len(BIN(opt_case_dispatch))
	                 + else_offset;
	    JUMP(else_offset);
	    emit_jump(builder, dst, 1);
	}
    }
}

static void record_opt_not(lir_builder_t *builder, jit_event_t *e)
{
    jit_snapshot_t *snapshot;
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    VALUE recv = TOPN(0);
    extern VALUE rb_obj_not(VALUE obj);
    vm_search_method(ci, recv);

    snapshot = take_snapshot(builder, REG_PC);
    if (check_cfunc(ci->me, rb_obj_not)) {
	lir_t Rrecv = _TOPN(0);
	EmitIR(GuardMethodCache, REG_PC, Rrecv, ci);
	_POP(); // pop Rrecv
	_PUSH(EmitIR(ObjectNot, Rrecv));
    } else {
	emit_call_method(builder, ci, snapshot, -1, NULL);
    }
}

static void record_opt_call_c_function(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_rectrace(lir_builder_t *builder, jit_event_t *e)
{
    assert(0 && "unreachable");
}

static void record_bitblt(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_answer(lir_builder_t *builder, jit_event_t *e)
{
    lir_t obj = emit_load_const(builder, INT2FIX(42));
    _PUSH(obj);
}

static void record_insn(lir_builder_t *builder, jit_event_t *e)
{
    int opcode = e->opcode;
    dump_inst(e);
#define CASE_RECORD(op)          \
    case BIN(op):                \
	record_##op(builder, e); \
	break
    switch (opcode) {
	CASE_RECORD(nop);
	CASE_RECORD(getlocal);
	CASE_RECORD(setlocal);
	CASE_RECORD(getspecial);
	CASE_RECORD(setspecial);
	CASE_RECORD(getinstancevariable);
	CASE_RECORD(setinstancevariable);
	CASE_RECORD(getclassvariable);
	CASE_RECORD(setclassvariable);
	CASE_RECORD(getconstant);
	CASE_RECORD(setconstant);
	CASE_RECORD(getglobal);
	CASE_RECORD(setglobal);
	CASE_RECORD(putnil);
	CASE_RECORD(putself);
	CASE_RECORD(putobject);
	CASE_RECORD(putspecialobject);
	CASE_RECORD(putiseq);
	CASE_RECORD(putstring);
	CASE_RECORD(concatstrings);
	CASE_RECORD(tostring);
	CASE_RECORD(toregexp);
	CASE_RECORD(newarray);
	CASE_RECORD(duparray);
	CASE_RECORD(expandarray);
	CASE_RECORD(concatarray);
	CASE_RECORD(splatarray);
	CASE_RECORD(newhash);
	CASE_RECORD(newrange);
	CASE_RECORD(pop);
	CASE_RECORD(dup);
	CASE_RECORD(dupn);
	CASE_RECORD(swap);
	CASE_RECORD(reput);
	CASE_RECORD(topn);
	CASE_RECORD(setn);
	CASE_RECORD(adjuststack);
	CASE_RECORD(defined);
	CASE_RECORD(checkmatch);
	CASE_RECORD(checkkeyword);
	CASE_RECORD(trace);
	CASE_RECORD(defineclass);
	CASE_RECORD(send);
	CASE_RECORD(opt_str_freeze);
	CASE_RECORD(opt_send_without_block);
	CASE_RECORD(invokesuper);
	CASE_RECORD(invokeblock);
	CASE_RECORD(leave);
	CASE_RECORD(throw);
	CASE_RECORD(jump);
	CASE_RECORD(branchif);
	CASE_RECORD(branchunless);
	CASE_RECORD(getinlinecache);
	CASE_RECORD(setinlinecache);
	CASE_RECORD(once);
	CASE_RECORD(opt_case_dispatch);
	CASE_RECORD(opt_plus);
	CASE_RECORD(opt_minus);
	CASE_RECORD(opt_mult);
	CASE_RECORD(opt_div);
	CASE_RECORD(opt_mod);
	CASE_RECORD(opt_eq);
	CASE_RECORD(opt_neq);
	CASE_RECORD(opt_lt);
	CASE_RECORD(opt_le);
	CASE_RECORD(opt_gt);
	CASE_RECORD(opt_ge);
	CASE_RECORD(opt_ltlt);
	CASE_RECORD(opt_aref);
	CASE_RECORD(opt_aset);
	CASE_RECORD(opt_aset_with);
	CASE_RECORD(opt_aref_with);
	CASE_RECORD(opt_length);
	CASE_RECORD(opt_size);
	CASE_RECORD(opt_empty_p);
	CASE_RECORD(opt_succ);
	CASE_RECORD(opt_not);
	CASE_RECORD(opt_regexpmatch1);
	CASE_RECORD(opt_regexpmatch2);
	CASE_RECORD(opt_call_c_function);
	CASE_RECORD(rectrace);
	CASE_RECORD(bitblt);
	CASE_RECORD(answer);
	CASE_RECORD(getlocal_OP__WC__0);
	CASE_RECORD(getlocal_OP__WC__1);
	CASE_RECORD(setlocal_OP__WC__0);
	CASE_RECORD(setlocal_OP__WC__1);
	CASE_RECORD(putobject_OP_INT2FIX_O_0_C_);
	CASE_RECORD(putobject_OP_INT2FIX_O_1_C_);
	default:
	    assert(0 && "unreachable");
    }
}
