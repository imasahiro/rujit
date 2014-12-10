/**********************************************************************

  record.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

//#define __int3__ __asm__ __volatile__("int3")
//#undef GET_GLOBAL_CONSTANT_STATE
//#define GET_GLOBAL_CONSTANT_STATE() (*jit_runtime.global_constant_state)
//
//#define EmitIR(OP, ...) Emit_##OP(rec, ##__VA_ARGS__)
//#define _POP() StackPop(rec)
//#define _PUSH(REG) StackPush(rec, REG)
//#define _TOPN(N) regstack_top(&(rec)->regstack, (int)(N))
//#define _SET(N, REG) regstack_set(&(rec)->regstack, (int)(N), REG)
//#define IS_Fixnum(V) FIXNUM_P(V)
//#define IS_Float(V) FLONUM_P(V)
//#define IS_String(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cString))
//#define IS_Array(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cArray))
//#define IS_Hash(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cHash))
//#define IS_Math(V) (!SPECIAL_CONST_P(V) && (RBASIC_CLASS(V) == rb_cMath))
//#define IS_Object(V) (!SPECIAL_CONST_P(V) && (RB_TYPE_P(V, T_OBJECT)))
//#define IS_NonSpecialConst(V) (!SPECIAL_CONST_P(V))
//#define CURRENT_PC get_current_pc(e, snapshot)
//
//#define not_support_op(rec, E, OPNAME)                               \
//    do {                                                             \
//	static const char msg[] = "not support bytecode: " OPNAME;   \
//	trace_recorder_abort(rec, e, TRACE_ERROR_UNSUPPORT_OP, msg); \
//	return;                                                      \
//    } while (0)
//
//typedef regstack_t jit_snapshot_t;
//
//static VALUE *get_current_pc(jit_event_t *e, jit_snapshot_t *snapshot)
//{
//    (void)snapshot;
//    return e->pc;
//}
//

static void record_nop(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_getlocal(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setlocal(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

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

static void record_getglobal(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setglobal(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putnil(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putself(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putobject(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putspecialobject(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putiseq(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putstring(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_concatstrings(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_tostring(lir_builder_t *builder, jit_event_t *e)
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

static void record_pop(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_dup(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_dupn(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_swap(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_reput(lir_builder_t *builder, jit_event_t *e)
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

static void record_trace(lir_builder_t *builder, jit_event_t *e)
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

static void record_opt_str_freeze(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_send_without_block(lir_builder_t *builder, jit_event_t *e)
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

static void record_jump(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_branchif(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
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

static void record_opt_plus(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_minus(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_mult(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_div(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_mod(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_eq(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_neq(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_lt(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_le(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_gt(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_ge(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_ltlt(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_aref(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_aset(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_aset_with(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_aref_with(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_length(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_size(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_empty_p(lir_builder_t *builder, jit_event_t *e)
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

static void record_opt_regexpmatch1(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_opt_regexpmatch2(lir_builder_t *builder, jit_event_t *e)
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

static void record_getlocal_OP__WC__0(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_getlocal_OP__WC__1(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setlocal_OP__WC__0(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_setlocal_OP__WC__1(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putobject_OP_INT2FIX_O_0_C_(lir_builder_t *builder, jit_event_t *e)
{
    TODO("");
}

static void record_putobject_OP_INT2FIX_O_1_C_(lir_builder_t *builder, jit_event_t *e)
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
