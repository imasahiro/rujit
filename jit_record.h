/**********************************************************************

  record.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#undef GET_GLOBAL_CONSTANT_STATE
#define GET_GLOBAL_CONSTANT_STATE() (*jit_runtime.global_constant_state)

#define _POP() lir_builder_pop(builder)
#define _PUSH(REG) lir_builder_push(builder, REG)
#define _TOPN(N) regstack_top(&(rec)->regstack, (int)(N))
#define _SET(N, REG) regstack_set(&(rec)->regstack, (int)(N), REG)
#define EmitIR(OP, ...) Emit_##OP(builder, ##__VA_ARGS__)
#define IS_Fixnum(V) FIXNUM_P(V)
#define IS_Float(V) FLONUM_P(V)
#define IS_String(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cString))
#define IS_Array(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cArray))
#define IS_Hash(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cHash))
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

typedef void jit_snapshot_t;

static jit_snapshot_t *take_snapshot(lir_builder_t *builder, VALUE *pc)
{
    fprintf(stderr, "snapshot %p\n", pc);
    return NULL;
}

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

static lir_t emit_call_method(lir_builder_t *builder, CALL_INFO ci)
{
    TODO("");
    return NULL;
}

static lir_t emit_get_prop(lir_builder_t *builder, CALL_INFO ci, lir_t recv)
{
    TODO("");
    return NULL;
}

static lir_t emit_set_prop(lir_builder_t *builder, CALL_INFO ci, lir_t recv, lir_t obj)
{
    TODO("");
    return NULL;
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
    lir_builder_add_const(builder, val, Rval);
    if (inst_size > 0 && inst_size != basicblock_size(entry_bb)) {
	basicblock_swap_inst(entry_bb, inst_size - 1, inst_size);
    }
    lir_builder_set_bb(builder, cur_bb);
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
    TODO("");
}

static void record_setinstancevariable(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
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

static void record_putspecialobject(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_concatstrings(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_toregexp(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_newarray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_duparray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_expandarray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_concatarray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_splatarray(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_newhash(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_newrange(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_dupn(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_topn(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setn(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_adjuststack(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_defined(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_checkmatch(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
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
    TODO("");
}

static void record_invokesuper(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_invokeblock(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_leave(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_throw(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void EmitJump(lir_builder_t *builder, VALUE *pc, int create_block)
{
    basicblock_t *bb = NULL;
    if (create_block == 0) {
	bb = lir_builder_create_block(builder, pc);
    }
    else {
	if ((bb = lir_builder_find_block(builder, pc)) == NULL) {
	    bb = lir_builder_create_block(builder, pc);
	}
    }
    assert(bb != NULL);
    EmitIR(Jump, bb);
    lir_builder_set_bb(builder, bb);
}

static void record_jump(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_branchif(lir_builder_t *builder, jit_event_t *e)
{
    OFFSET dst = (OFFSET)GET_OPERAND(1);
    lir_t Rval = _POP();
    VALUE val = TOPN(0);
    VALUE *next_pc = e->pc + insn_len(BIN(branchif));
    VALUE *jump_pc = next_pc + dst;
    jit_event_t e2;
    lir_t Rguard = NULL;
    if (RTEST(val)) {
	e2.pc = jump_pc;
	take_snapshot(builder, next_pc);
	Rguard = EmitIR(GuardTypeNil, next_pc, Rval);
	EmitJump(builder, jump_pc, 1);
    }
    else {
	e2.pc = next_pc;
	take_snapshot(builder, jump_pc);
	Rguard = EmitIR(GuardTypeNonNil, jump_pc, Rval);
	EmitJump(builder, next_pc, 1);
    }
    if (already_recorded_on_trace(&e2)) {
	lir_set(Rguard, LIR_FLAG_TRACE_EXIT);
    }
}

static void record_branchunless(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_getinlinecache(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setinlinecache(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_once(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_case_dispatch(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_succ(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_not(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_call_c_function(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_rectrace(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_bitblt(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_answer(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
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
