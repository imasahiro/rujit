/**********************************************************************

  record.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#undef GET_GLOBAL_CONSTANT_STATE
#define GET_GLOBAL_CONSTANT_STATE() (*jit_runtime.global_constant_state)

#define EmitIR(OP, ...) Emit_##OP(rec, ##__VA_ARGS__)
#define _POP() StackPop(rec)
#define _PUSH(REG) StackPush(rec, REG)
#define _TOPN(N) regstack_top(&(rec)->regstack, (int)(N))
#define _SET(N, REG) regstack_set(&(rec)->regstack, (int)(N), REG)
#define IS_Fixnum(V) FIXNUM_P(V)
#define IS_Float(V) FLONUM_P(V)
#define IS_String(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cString))
#define IS_Array(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cArray))
#define IS_Hash(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cHash))
#define IS_Math(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cMath))
#define IS_Object(V) (!SPECIAL_CONST_P(V) && (RB_TYPE_P(V, T_OBJECT)))
#define IS_NonSpecialConst(V) (!SPECIAL_CONST_P(V))
#define CURRENT_PC get_current_pc(e, snapshot)

#define not_support_op(rec, E, OPNAME)                               \
    do {                                                             \
	static const char msg[] = "not support bytecode: " OPNAME;   \
	trace_recorder_abort(rec, e, TRACE_ERROR_UNSUPPORT_OP, msg); \
	return;                                                      \
    } while (0)

typedef regstack_t jit_snapshot_t;

static VALUE *get_current_pc(jit_event_t *e, jit_snapshot_t *snapshot)
{
    (void)snapshot;
    return e->pc;
}

static jit_snapshot_t *take_snapshot(trace_recorder_t *rec)
{
    jit_event_t *e = rec->current_event;
    return trace_recorder_take_snapshot(rec, REG_PC, 0);
}

static lir_t emit_get_prop(trace_recorder_t *rec, CALL_INFO ci, lir_t recv)
{
    return NULL;
}

static lir_t emit_set_prop(trace_recorder_t *rec, CALL_INFO ci, lir_t recv, lir_t val)
{
    return NULL;
}

static lir_t emit_call_method(trace_recorder_t *rec, CALL_INFO ci)
{
    return NULL;
}

static lir_t StackPop(trace_recorder_t *rec)
{
    int popped = 0;
    lir_t Rval = regstack_pop(rec, &(rec)->regstack, &popped);
    //if (popped == 0) {
    //    EmitIR(StackPop);
    //}
    return Rval;
}

static void StackPush(trace_recorder_t *rec, lir_t Rval)
{
    regstack_push(rec, &(rec)->regstack, Rval);
    // EmitIR(StackPush, Rval);
}

static lir_t emit_load_const(trace_recorder_t *rec, VALUE val)
{
    unsigned inst_size;
    basicblock_t *BB = rec->entry_bb;
    basicblock_t *prevBB = rec->cur_bb;
    lir_t Rval = trace_recorder_get_const(rec, val);
    if (Rval) {
	return Rval;
    }
    rec->cur_bb = BB;
    inst_size = basicblock_size(BB);
    if (NIL_P(val)) {
	Rval = EmitIR(LoadConstNil);
    }
    else if (val == Qtrue || val == Qfalse) {
	Rval = EmitIR(LoadConstBoolean, val);
    }
    else if (FIXNUM_P(val)) {
	Rval = EmitIR(LoadConstFixnum, val);
    }
    else if (FLONUM_P(val)) {
	Rval = EmitIR(LoadConstFloat, val);
    }
    else if (!SPECIAL_CONST_P(val)) {
	if (RBASIC_CLASS(val) == rb_cString) {
	    Rval = EmitIR(LoadConstString, val);
	}
	else if (RBASIC_CLASS(val) == rb_cRegexp) {
	    Rval = EmitIR(LoadConstRegexp, val);
	}
    }

    if (Rval == NULL) {
	Rval = EmitIR(LoadConstObject, val);
    }
    trace_recorder_add_const(rec, val, Rval);
    if (inst_size > 0 && inst_size != basicblock_size(BB)) {
	basicblock_swap_inst(BB, inst_size - 1, inst_size);
    }
    rec->cur_bb = prevBB;
    return Rval;
}

static lir_t trace_recorder_get_localvar(trace_recorder_t *rec, basicblock_t *bb, int lev, int idx)
{
    return variable_table_get(bb->last_table, idx, lev, 0);
}

static lir_t trace_recorder_get_self(trace_recorder_t *rec, basicblock_t *bb)
{
    return variable_table_get_self(bb->last_table, 0);
}

static lir_t trace_recorder_set_self(trace_recorder_t *rec, basicblock_t *bb, lir_t val)
{
    variable_table_set_self(bb->last_table, 0, val);
    return val;
}

static void trace_recorder_set_localvar(trace_recorder_t *rec, basicblock_t *bb, int level, int idx, lir_t val)
{
    variable_table_set(bb->last_table, idx, level, 0, val);
    if (level > 0) {
	variable_table_t *vt = bb->last_table;
	rb_control_frame_t *cfp = rec->current_event->cfp;
	rb_control_frame_t *cfp1 = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
	unsigned nest_level = 1;
	while (vt != NULL) {
	    if (cfp->iseq->parent_iseq == cfp1->iseq) {
		cfp = cfp1;
		level--;
	    }
	    if (level == 0) {
		break;
	    }
	    vt = vt->next;
	    nest_level++;
	    cfp1 = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp1);
	}
	if (vt) {
	    if (variable_table_depth(vt) < nest_level) {
		trace_recorder_insert_vtable(rec, nest_level);
	    }
	    variable_table_set(bb->last_table, idx, level, nest_level, val);
	}
    }
}

#define CREATE_BLOCK(REC, PC) trace_recorder_create_block(REC, PC)

static void EmitJump(trace_recorder_t *rec, VALUE *pc, int link)
{
    basicblock_t *bb = NULL;
    if (link == 0) {
	bb = CREATE_BLOCK(rec, pc);
    }
    else {
	if ((bb = trace_recorder_get_block(rec, pc)) == NULL) {
	    bb = CREATE_BLOCK(rec, pc);
	}
    }
    EmitIR(Jump, bb);
    rec->cur_bb = bb;
}

static lir_t EmitEnvLoad(trace_recorder_t *rec, int level, int idx)
{
    lir_t Rval = EmitIR(EnvLoad, (int)level, (int)idx);
    trace_recorder_set_localvar(rec, rec->cur_bb, (int)level, (int)idx, Rval);
    return Rval;
}

static lir_t EmitEnvStore(trace_recorder_t *rec, int level, int idx, lir_t Rval)
{
    Rval = EmitIR(EnvStore, (int)level, (int)idx, Rval);
    trace_recorder_set_localvar(rec, rec->cur_bb, (int)level, (int)idx, Rval);
    return Rval;
}

#include "bc2lir.c"

static void record_getspecial(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "getspecial");
}

static void record_setspecial(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "setspecial");
}

static void record_getclassvariable(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "getclassvariable");
}

static void record_setclassvariable(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "setclassvariable");
}

static void record_getconstant(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "getconstant");
}

static void record_setconstant(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "setconstant");
}

static void record_getinstancevariable(trace_recorder_t *rec, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    ID id = (ID)GET_OPERAND(1);
    VALUE obj = GET_SELF();
    lir_t Rrecv = trace_recorder_set_self(rec, rec->cur_bb, EmitIR(LoadSelf));

    if (vm_load_cache(obj, id, ic, NULL, 0)) {
	size_t index = ic->ic_value.index;
	trace_recorder_take_snapshot(rec, REG_PC, 0);
	EmitIR(GuardTypeObject, REG_PC, Rrecv);
	EmitIR(GuardProperty, REG_PC, Rrecv, 0 /*!is_attr*/, id, index, ic->ic_serial);
	_PUSH(EmitIR(GetPropertyName, Rrecv, index));
	return;
    }
    not_support_op(rec, e, "getinstancevariable");
}

static void record_setinstancevariable(trace_recorder_t *rec, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    ID id = (ID)GET_OPERAND(1);
    VALUE val = TOPN(0);
    VALUE obj = GET_SELF();
    lir_t Rrecv = trace_recorder_set_self(rec, rec->cur_bb, EmitIR(LoadSelf));

    int cacheable = vm_load_or_insert_ivar(obj, id, val, ic, NULL, 0);
    if (cacheable) {
	lir_t Rval;
	size_t index = ic->ic_value.index;
	trace_recorder_take_snapshot(rec, REG_PC, 0);
	EmitIR(GuardTypeObject, REG_PC, Rrecv);
	EmitIR(GuardProperty, REG_PC, Rrecv, 0 /*!is_attr*/, id, index, ic->ic_serial);
	Rval = _POP();
	if (cacheable == 1) {
	    EmitIR(SetPropertyName, Rrecv, 0, ic->ic_value.index, Rval);
	}
	else {
	    EmitIR(SetPropertyName, Rrecv, (long)id, ic->ic_value.index, Rval);
	}
	return;
    }
    not_support_op(rec, e, "setinstancevariable");
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

static void record_putspecialobject(trace_recorder_t *rec, jit_event_t *e)
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

static void record_concatstrings(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t num = (rb_num_t)GET_OPERAND(1);
    rb_num_t i = num - 1;

    lir_t Rval = EmitIR(AllocString, _TOPN(i));
    while (i-- > 0) {
	Rval = EmitIR(StringAdd, Rval, _TOPN(i));
    }
    for (i = 0; i < num; i++) {
	_POP();
    }
    _PUSH(Rval);
}

static void record_toregexp(trace_recorder_t *rec, jit_event_t *e)
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

static void record_newarray(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    lir_t argv[num];
    for (i = 0; i < num; i++) {
	argv[i] = _POP();
    }
    _PUSH(EmitIR(AllocArray, (int)num, argv));
}

static void record_duparray(trace_recorder_t *rec, jit_event_t *e)
{
    VALUE val = (VALUE)GET_OPERAND(1);
    lir_t Rval = emit_load_const(rec, val);
    _PUSH(EmitIR(ArrayDup, Rval));
}

static void record_expandarray(trace_recorder_t *rec, jit_event_t *e)
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
	not_support_op(rec, e, "expandarray");
	return;
    }
    Rary = _TOPN(0);
    trace_recorder_take_snapshot(rec, REG_PC, 0);
    EmitIR(GuardTypeArray, REG_PC, Rary);
    EmitIR(GuardArraySize, REG_PC, Rary, len);
    _POP(); // Rary

    Rnil = emit_load_const(rec, Qnil);
    if (flag & 0x02) {
	/* post: ..., nil ,ary[-1], ..., ary[0..-num] # top */
	rb_num_t i = 0, j;

	if (len < num) {
	    for (i = 0; i < num - len; i++) {
		_PUSH(Rnil);
	    }
	}
	for (j = 0; i < num; i++, j++) {
	    lir_t Ridx = emit_load_const(rec, LONG2FIX(len - j - 1));
	    _PUSH(EmitIR(ArrayGet, Rary, Ridx));
	}
	if (is_splat) {
	    i = 0;
	    for (; j < len; ++j) {
		lir_t Ridx = emit_load_const(rec, LONG2FIX(i));
		regs[i] = EmitIR(ArrayGet, Rary, Ridx);
		i++;
	    }
	    _PUSH(EmitIR(AllocArray, (int)i, regs));
	}
    }
    else {
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
	    Ridx = emit_load_const(rec, LONG2FIX(i));
	    regs2[i] = EmitIR(ArrayGet, Rary, Ridx);
	}
	for (i = num; i > 0; i--) {
	    _PUSH(regs2[i - 1]);
	}
	if (is_splat) {
	    if (num > len) {
		_PUSH(EmitIR(AllocArray, 0, regs));
	    }
	    else {
		for (i = 0; i < len - num; ++i) {
		    lir_t Ridx = emit_load_const(rec, LONG2FIX(i + num));
		    regs[i] = EmitIR(ArrayGet, Rary, Ridx);
		}
		_PUSH(EmitIR(AllocArray, (int)(len - num), regs));
	    }
	}
    }
    RB_GC_GUARD(ary);
}

static void record_concatarray(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "concatarray");
}

static void record_splatarray(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "splatarray");
}

static void record_newhash(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t i, num = (rb_num_t)GET_OPERAND(1);
    lir_t argv[num];
    for (i = num; i > 0; i -= 2) {
	argv[i - 1] = _POP(); // key
	argv[i - 2] = _POP(); // val
    }
    _PUSH(EmitIR(AllocHash, (int)num, argv));
}

static void record_newrange(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t flag = (rb_num_t)GET_OPERAND(1);
    lir_t Rhigh = _POP();
    lir_t Rlow = _POP();
    _PUSH(EmitIR(AllocRange, Rlow, Rhigh, (int)flag));
}

static void record_dupn(trace_recorder_t *rec, jit_event_t *e)
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

static void record_topn(trace_recorder_t *rec, jit_event_t *e)
{
    lir_t Rval;
    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    assert(0 && "need to test");
    Rval = _TOPN(n);
    _PUSH(Rval);
}

static void record_setn(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t n = (rb_num_t)GET_OPERAND(1);
    lir_t Rval = _TOPN(0);
    _SET(n, Rval);
}

static void record_adjuststack(trace_recorder_t *rec, jit_event_t *e)
{
    rb_num_t i, n = (rb_num_t)GET_OPERAND(1);
    for (i = 0; i < n; i++) {
	_POP();
    }
}

static void record_defined(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "defined");
}

static void record_checkmatch(trace_recorder_t *rec, jit_event_t *e)
{
    lir_t Rpattern = _POP();
    lir_t Rtarget = _POP();
    rb_event_flag_t flag = (rb_event_flag_t)GET_OPERAND(1);
    enum vm_check_match_type checkmatch_type
        = (enum vm_check_match_type)(flag & VM_CHECKMATCH_TYPE_MASK);
    if (flag & VM_CHECKMATCH_ARRAY) {
	_PUSH(EmitIR(PatternMatchRange, Rpattern, Rtarget, checkmatch_type));
    }
    else {
	_PUSH(EmitIR(PatternMatch, Rpattern, Rtarget, checkmatch_type));
    }
}

static void record_defineclass(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "defineclass");
}

#include "jit_args.h"

static void record_send(trace_recorder_t *rec, jit_event_t *e)
{
    asm volatile("int3");
}

static void record_invokesuper(trace_recorder_t *rec, jit_event_t *e)
{
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    lir_t Rblock = 0;
    rb_block_t *block = NULL;

    ci->argc = ci->orig_argc;
    // ci->blockptr = !(ci->flag & VM_CALL_ARGS_BLOCKARG) ? GET_BLOCK_PTR() : 0;
    if (UNLIKELY(ci->flag & VM_CALL_ARGS_BLOCKARG)) {
	not_support_op(rec, e, "invokesuper");
	return;
    }
    else if (ci->blockiseq != 0) {
	ci->blockptr = RUBY_VM_GET_BLOCK_PTR_IN_CFP(REG_CFP);
	ci->blockptr->iseq = ci->blockiseq;
	ci->blockptr->proc = 0;
	Rblock = EmitIR(LoadSelfAsBlock, ci->blockiseq);
	block = ci->blockptr;
    }
    trace_recorder_take_snapshot(rec, REG_PC, 0);
    asm volatile("int3");
    //EmitMethodCall(rec, e, ci, block, Rblock, 1);
}

static void record_invokeblock(trace_recorder_t *rec, jit_event_t *e)
{
    const rb_block_t *block;
    CALL_INFO ci = (CALL_INFO)GET_OPERAND(1);
    int i, argc = 1 /*recv*/ + ci->orig_argc;
    lir_t Rblock, ret;
    lir_t regs[ci->orig_argc + 1];

    VALUE type;

    trace_recorder_take_snapshot(rec, REG_PC, 0);
    block = rb_vm_control_frame_block_ptr(REG_CFP);

    ci->argc = ci->orig_argc;
    ci->blockptr = 0;
    ci->recv = GET_SELF();

    type = GET_ISEQ()->local_iseq->type;

    if ((type != ISEQ_TYPE_METHOD && type != ISEQ_TYPE_CLASS) || block == 0) {
	trace_recorder_abort(rec, e, TRACE_ERROR_THROW, "no block given (yield)");
	return;
    }

    if (UNLIKELY(ci->flag & VM_CALL_ARGS_SPLAT)) {
	trace_recorder_abort(rec, e, TRACE_ERROR_UNSUPPORT_OP,
	                     "not supported: VM_CALL_ARGS_SPLAT");
	return;
    }

    if (BUILTIN_TYPE(block->iseq) == T_NODE) {
	trace_recorder_abort(rec, e, TRACE_ERROR_NATIVE_METHOD,
	                     "yield native block");
	return;
    }

    regs[0] = trace_recorder_set_self(rec, rec->cur_bb, EmitIR(LoadSelf));
    Rblock = EmitIR(LoadBlock);
    EmitIR(GuardBlockEqual, REG_PC, Rblock, block->iseq);
    if (block) {
	jit_regsiter_block(rec->trace, block);
    }
    for (i = 0; i < ci->orig_argc; i++) {
	regs[ci->orig_argc - i] = _POP();
    }

    EmitJump(rec, REG_PC, 0);
    ret = EmitIR(InvokeBlock, REG_PC, ci, Rblock, argc, regs);
    trace_recorder_push_variable_table(rec, ret);
}

static void record_leave(trace_recorder_t *rec, jit_event_t *e)
{
    lir_t Val;
    variable_table_t *vtable;
    IInvokeMethod *inst;
    if ((vtable = trace_recorder_pop_variable_table(rec)) == NULL) {
	trace_recorder_abort(rec, e, TRACE_ERROR_LEAVE, "");
	return;
    }
    if (vtable->first_inst == NULL) {
	trace_recorder_abort(rec, e, TRACE_ERROR_LEAVE, "");
	return;
    }

    inst = (IInvokeMethod *)vtable->first_inst;
    if (inst && inst->base.opcode != OPCODE_IInvokeConstructor && VM_FRAME_TYPE_FINISH_P(REG_CFP)) {
	trace_recorder_abort(rec, e, TRACE_ERROR_LEAVE, "");
	return;
    }

    Val = _POP();
    if (inst && inst->base.opcode == OPCODE_IInvokeConstructor) {
	Val = EmitIR(LoadSelf);
    }
    EmitIR(FramePop);
    EmitJump(rec, REG_PC, 0);
    _PUSH(Val);
}

static void record_throw(trace_recorder_t *rec, jit_event_t *e)
{
    // unreachable
    not_support_op(rec, e, "throw");
}

static void record_jump(trace_recorder_t *rec, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    VALUE *target_pc = REG_PC + insn_len(BIN(jump)) + dst;
    EmitJump(rec, target_pc, 1);
}

static void record_branchif(trace_recorder_t *rec, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    lir_t Rval = _POP();
    VALUE val = TOPN(0);
    VALUE *next_pc = e->pc + insn_len(BIN(branchif));
    VALUE *jump_pc = next_pc + dst;
    int force_exit = jump_pc == rec->trace->last_pc;
    if (RTEST(val)) {
	trace_recorder_take_snapshot(rec, next_pc, force_exit);
	EmitIR(GuardTypeNil, next_pc, Rval);
	EmitJump(rec, jump_pc, 1);
    }
    else {
	trace_recorder_take_snapshot(rec, jump_pc, force_exit);
	EmitIR(GuardTypeNonNil, jump_pc, Rval);
	EmitJump(rec, next_pc, 1);
    }
}

static void record_branchunless(trace_recorder_t *rec, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    lir_t Rval = _POP();
    VALUE val = TOPN(0);
    VALUE *next_pc = REG_PC + insn_len(BIN(branchunless));
    VALUE *jump_pc = next_pc + dst;
    VALUE *target_pc = NULL;

    if (!RTEST(val)) {
	trace_recorder_take_snapshot(rec, next_pc, 0);
	EmitIR(GuardTypeNonNil, next_pc, Rval);
	target_pc = jump_pc;
    }
    else {
	trace_recorder_take_snapshot(rec, jump_pc, 0);
	EmitIR(GuardTypeNil, jump_pc, Rval);
	target_pc = next_pc;
    }

    EmitJump(rec, target_pc, 1);
}

static void record_getinlinecache(trace_recorder_t *rec, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    if (ic->ic_serial != GET_GLOBAL_CONSTANT_STATE()) {
	// constant value is re-defined.
	not_support_op(rec, e, "getinlinecache");
	return;
    }
    _PUSH(emit_load_const(rec, ic->ic_value.value));
}

static void record_setinlinecache(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "setinlinecache");
}

static void record_once(trace_recorder_t *rec, jit_event_t *e)
{
    IC ic = (IC)GET_OPERAND(2);
    //ISEQ iseq = (ISEQ)GET_OPERAND(1);
    union iseq_inline_storage_entry *is = (union iseq_inline_storage_entry *)ic;

#define RUNNING_THREAD_ONCE_DONE ((rb_thread_t *)(0x1))
    if (is->once.running_thread != RUNNING_THREAD_ONCE_DONE) {
	not_support_op(rec, e, "once");
    }
    else {
	_PUSH(emit_load_const(rec, is->once.value));
    }
}

static void record_opt_case_dispatch(trace_recorder_t *rec, jit_event_t *e)
{
    lir_t Rkey = _TOPN(0);
    OFFSET else_offset = (OFFSET)GET_OPERAND(2);
    CDHASH hash = (CDHASH)GET_OPERAND(1);
    VALUE key = TOPN(0);
    int type;
    st_data_t val;

    trace_recorder_take_snapshot(rec, REG_PC, 0);
    type = TYPE(key);
    switch (type) {
	case T_FLOAT: {
	    // FIXME
	    not_support_op(rec, e, "opt_case_dispatch");
	    //double ival;
	    //if (modf(RFLOAT_VALUE(key), &ival) == 0.0) {
	    //  key = FIXABLE(ival) ? LONG2FIX((long)ival) : rb_dbl2big(ival);
	    //}
	}
	case T_SYMBOL: /* fall through */
	case T_FIXNUM:
	case T_BIGNUM:
	case T_STRING:
	    if (BASIC_OP_UNREDEFINED_P(BOP_EQQ,
	                               SYMBOL_REDEFINED_OP_FLAG | FIXNUM_REDEFINED_OP_FLAG | BIGNUM_REDEFINED_OP_FLAG | STRING_REDEFINED_OP_FLAG)) {
		if (type == T_SYMBOL) {
		    EmitIR(GuardTypeSymbol, REG_PC, Rkey);
		    EmitIR(GuardMethodRedefine, REG_PC, SYMBOL_REDEFINED_OP_FLAG, BOP_EQQ);
		}
		else if (type == T_FIXNUM) {
		    EmitIR(GuardTypeFixnum, REG_PC, Rkey);
		    EmitIR(GuardMethodRedefine, REG_PC, FIXNUM_REDEFINED_OP_FLAG, BOP_EQQ);
		}
		else if (type == T_BIGNUM) {
		    EmitIR(GuardTypeBignum, REG_PC, Rkey);
		    EmitIR(GuardMethodRedefine, REG_PC, BIGNUM_REDEFINED_OP_FLAG, BOP_EQQ);
		}
		else if (type == T_STRING) {
		    EmitIR(GuardTypeString, REG_PC, Rkey);
		    EmitIR(GuardMethodRedefine, REG_PC, STRING_REDEFINED_OP_FLAG, BOP_EQQ);
		}
		else {
		    assert(0 && "unreachable");
		}
		_POP(); // pop Rkey
		// We assume `hash` is constant variable
		if (st_lookup(RHASH_TBL_RAW(hash), key, &val)) {
		    VALUE *dst = REG_PC + insn_len(BIN(opt_case_dispatch))
		                 + FIX2INT((VALUE)val);
		    EmitJump(rec, dst, 1);
		}
		else {
		    VALUE *dst = REG_PC + insn_len(BIN(opt_case_dispatch))
		                 + else_offset;
		    JUMP(else_offset);
		    EmitJump(rec, dst, 1);
		}
		break;
	    }
	default:
	    break;
    }
}

static void record_opt_call_c_function(trace_recorder_t *rec, jit_event_t *e)
{
    not_support_op(rec, e, "opt_call_c_function");
}

static void record_bitblt(trace_recorder_t *rec, jit_event_t *e)
{
    VALUE str = rb_str_new2("a bit of bacon, lettuce and tomato");
    _PUSH(emit_load_const(rec, str));
}

static void record_answer(trace_recorder_t *rec, jit_event_t *e)
{
    _PUSH(emit_load_const(rec, INT2FIX(42)));
}

static void record_opt_succ(trace_recorder_t *rec, jit_event_t *e)
{
    asm volatile("int3");
}

static void record_opt_not(trace_recorder_t *rec, jit_event_t *e)
{
    asm volatile("int3");
}

static void record_insn(trace_recorder_t *rec, jit_event_t *e)
{
    int opcode = e->opcode;
    dump_inst(e);
#define CASE_RECORD(op)      \
    case BIN(op):            \
	record_##op(rec, e); \
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
