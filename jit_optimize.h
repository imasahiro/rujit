/**********************************************************************

  optimizer.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

static lir_t emit_load_const(lir_builder_t *builder, VALUE val);
#define BB_PUSH_FRAME (1 << 0)
#define BB_POP_FRAME (1 << 1)

typedef VALUE (*lir_folder1_t)(VALUE);
typedef VALUE (*lir_folder2_t)(VALUE, VALUE);

static void update_side_exit_reference_count(lir_t inst)
{
    regstack_t *stack;
    basicblock_t *bb = inst->parent;
    VALUE *pc = ((IGuardTypeFixnum *)inst)->Exit;
    int idx = jit_list_indexof(&bb->stack_map, (uintptr_t)pc);
    assert(0 <= idx);
    stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
    RC_INC(stack);
}

static void lir_inst_replace_with(lir_builder_t *builder, lir_inst_t *inst, lir_inst_t *newinst)
{
    unsigned i, j, k;
    lir_inst_t **ref = NULL;
    lir_inst_t *user;
    // 1. replace argument of inst->user->list[x]
    if (inst->user) {
	for (i = 0; i < inst->user->size; i++) {
	    j = 0;
	    user = (lir_t)jit_list_get(inst->user, i);
	    while ((ref = lir_inst_get_args(user, j)) != NULL) {
		if (ref && *ref == inst) {
		    *ref = newinst;
		    lir_inst_adduser(builder, newinst, user);
		}
		j += 1;
	    }
	}
	jit_list_delete(inst->user);
	inst->user = NULL;
    }

    // 2. replace side exit and variable_table
    for (k = 0; k < lir_builder_blocks(rec)->size; k++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, k);
	// 2.1. replace side exit
	for (j = 0; j < GET_STACK_MAP_ENTRY_SIZE(&bb->stack_map); j++) {
	    regstack_t *stack;
	    unsigned idx = GET_STACK_MAP_REAL_INDEX(j);
	    stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	    for (i = 0; i < stack->list.size; i++) {
		if (inst == regstack_get_direct(stack, i)) {
		    regstack_set_direct(stack, i, newinst);
		}
	    }
	}
	// 2.2. replace variable_table
	if (bb->init_table->self == inst) {
	    bb->init_table->self = newinst;
	}
	if (bb->last_table->self == inst) {
	    bb->last_table->self = newinst;
	}
	for (j = 0; j < bb->init_table->table.size; j += 3) {
	    lir_t reg = (lir_t)jit_list_get(&bb->init_table->table, j + 2);
	    if (reg == inst) {
		jit_list_set(&bb->init_table->table, j + 2, (uintptr_t)newinst);
	    }
	}
	for (j = 0; j < bb->last_table->table.size; j += 3) {
	    lir_t reg = (lir_t)jit_list_get(&bb->last_table->table, j + 2);
	    if (reg == inst) {
		jit_list_set(&bb->last_table->table, j + 2, (uintptr_t)newinst);
	    }
	}
    }
}

static int lir_inst_define_value(int opcode)
{
    switch (opcode) {
#define DEF_IR(OPNAME)     \
    case OPCODE_I##OPNAME: \
	return LIR_USE_##OPNAME;

	LIR_EACH(DEF_IR);
	default:
	    assert(0 && "unreachable");
    }
#undef DEF_IR
    return 0;
}

static int lir_inst_have_side_effect(int opcode)
{
#define DEF_SIDE_EFFECT(OPNAME) \
    case OPCODE_I##OPNAME:      \
	return LIR_SIDE_EFFECT_##OPNAME;
    switch (opcode) {
	LIR_EACH(DEF_SIDE_EFFECT);
	default:
	    assert(0 && "unreachable");
    }
#undef DEF_SIDE_EFFECT
    return 0;
}

static int is_constant(lir_inst_t *inst)
{
    switch (lir_opcode(inst)) {
	case OPCODE_ILoadConstNil:
	case OPCODE_ILoadConstObject:
	case OPCODE_ILoadConstBoolean:
	case OPCODE_ILoadConstFixnum:
	case OPCODE_ILoadConstFloat:
	case OPCODE_ILoadConstString:
	case OPCODE_ILoadConstRegexp:
	    return 1;
	default:
	    break;
    }
    return 0;
}

static int elimnate_guard(lir_builder_t *builder, lir_inst_t *inst)
{
    /* Remove guard that always true
     * e.g.) L2 is always true becuase L1 is fixnum. So we can remove L2.
     * L1 = LoadConstFixnum 10
     * L2 = GuardTypeFixnum L1 exit_pc
     */
    IGuardTypeFixnum *guard = (IGuardTypeFixnum *)inst;
    lir_inst_t *src;

    if (!lir_is_guard(inst)) {
	return 0;
    }
    src = guard->R;
    if (inst->opcode != OPCODE_IGuardMethodRedefine && lir_opcode(src) == OPCODE_IEnvStore) {
	src = ((IEnvStore *)src)->Val;
    }

#define RETURN_IF(INST1, OP1, INST2, OP2)         \
    if (lir_opcode(INST1) == OPCODE_I##OP1) {     \
	if (lir_opcode(INST2) == OPCODE_I##OP2) { \
	    return 1;                             \
	}                                         \
    }

    RETURN_IF(inst, GuardTypeFixnum, src, LoadConstFixnum);
    RETURN_IF(inst, GuardTypeRegexp, src, LoadConstRegexp);

    if (lir_opcode(inst) == OPCODE_IGuardTypeSymbol) {
	switch (lir_opcode(src)) {
	    case OPCODE_ILoadConstObject:
		if (SYMBOL_P(((ILoadConstObject *)src)->Val)) {
		    return 1;
		}
		break;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeFloat) {
	switch (lir_opcode(src)) {
	    case OPCODE_ILoadConstFloat:
		if (RB_FLOAT_TYPE_P(((ILoadConstFloat *)src)->Val)) {
		    return 1;
		}
		break;
	    case OPCODE_IFloatAdd:
	    case OPCODE_IFloatSub:
	    case OPCODE_IFloatMul:
	    case OPCODE_IFloatDiv:
	    case OPCODE_IFloatNeg:
	    case OPCODE_IFixnumToFloat:
	    case OPCODE_IStringToFloat:
	    case OPCODE_IMathSin:
	    case OPCODE_IMathCos:
	    case OPCODE_IMathTan:
	    case OPCODE_IMathExp:
	    case OPCODE_IMathSqrt:
	    case OPCODE_IMathLog10:
	    case OPCODE_IMathLog2:
		return 1;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeArray) {
	switch (lir_opcode(src)) {
	    case OPCODE_IAllocArray:
		return 1;
	    case OPCODE_IArrayDup:
		return 1;
	    case OPCODE_ILoadConstObject:
		if (RBASIC_CLASS(((ILoadConstObject *)src)->Val) == rb_cArray) {
		    return 1;
		}
		break;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeString) {
	if (lir_opcode(src) == OPCODE_ILoadConstString) {
	    return 1;
	}
	if (lir_opcode(src) == OPCODE_IAllocString) {
	    return 1;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardTypeSpecialConst) {
	switch (lir_opcode(src)) {
	    case OPCODE_ILoadConstNil:
	    case OPCODE_ILoadConstBoolean:
	    case OPCODE_ILoadConstFixnum:
		return 0;
	    case OPCODE_ILoadConstFloat:
		if (!RB_FLOAT_TYPE_P(((ILoadConstFloat *)src)->Val)) {
		    return 1;
		}
	    case OPCODE_ILoadConstObject:
	    case OPCODE_ILoadConstString:
	    case OPCODE_ILoadConstRegexp:
		return 1;
	    default:
		break;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardArraySize) {
	IGuardArraySize *ir1 = (IGuardArraySize *)inst;
	switch (lir_opcode(src)) {
	    case OPCODE_IAllocArray:
		return ir1->size == ((IAllocArray *)src)->argc;
	    case OPCODE_IArrayDup:
		if (lir_opcode(((IArrayDup *)src)->orig) == OPCODE_IAllocArray) {
		    IAllocArray *ir2 = (IAllocArray *)((IArrayDup *)src)->orig;
		    return ir1->size == ir2->argc;
		}
		return 1;
	    case OPCODE_ILoadConstObject:
		if (RBASIC_CLASS(((ILoadConstObject *)src)->Val) == rb_cArray) {
		    return ir1->size == RARRAY_LEN(src);
		}
		break;
	}
    }

    if (lir_opcode(inst) == OPCODE_IGuardBlockEqual) {
	if (lir_opcode(src) == OPCODE_IAllocBlock) {
	    IAllocBlock *ab = (IAllocBlock *)src;
	    if (lir_opcode(ab->block) == OPCODE_ILoadSelfAsBlock) {
		ISEQ iseq1 = ((IGuardBlockEqual *)inst)->iseq;
		ISEQ iseq2 = ((ILoadSelfAsBlock *)ab->block)->iseq;
		if (iseq1 == iseq2) {
		    return 1;
		}
	    }
	}
    }
    return 0;
}

static lir_inst_t *fold_binop_fixnum2(lir_builder_t *builder, lir_folder_t folder, lir_inst_t *inst)
{
    ILoadConstFixnum *LHS = (ILoadConstFixnum *)*lir_inst_get_args(inst, 0);
    ILoadConstFixnum *RHS = (ILoadConstFixnum *)*lir_inst_get_args(inst, 1);
    int lop = lir_opcode(&LHS->base);
    int rop = lir_opcode(&RHS->base);
    if (lop == OPCODE_IEnvStore) {
	LHS = (ILoadConstFixnum *)((IEnvStore *)LHS)->Val;
	lop = lir_opcode(&LHS->base);
    }
    if (rop == OPCODE_IEnvStore) {
	RHS = (ILoadConstFixnum *)((IEnvStore *)RHS)->Val;
	rop = lir_opcode(&RHS->base);
    }

    // const + const
    if (lop == OPCODE_ILoadConstFixnum && rop == OPCODE_ILoadConstFixnum) {
	VALUE val = ((lir_folder2_t)folder)(LHS->Val, RHS->Val);
	return emit_load_const(builder, val);
    }
    return inst;
}

static lir_inst_t *fold_binop_float2(lir_builder_t *builder, lir_folder_t folder, lir_inst_t *inst)
{
    ILoadConstFloat *LHS = (ILoadConstFloat *)*lir_inst_get_args(inst, 0);
    ILoadConstFloat *RHS = (ILoadConstFloat *)*lir_inst_get_args(inst, 1);
    int lop = lir_opcode(&LHS->base);
    int rop = lir_opcode(&RHS->base);
    if (lop == OPCODE_IEnvStore) {
	LHS = (ILoadConstFloat *)((IEnvStore *)LHS)->Val;
	lop = lir_opcode(&LHS->base);
    }
    if (rop == OPCODE_IEnvStore) {
	RHS = (ILoadConstFloat *)((IEnvStore *)RHS)->Val;
	rop = lir_opcode(&RHS->base);
    }

    // const + const
    if (lop == OPCODE_ILoadConstFloat && rop == OPCODE_ILoadConstFloat) {
	// FIXME need to insert GuardTypeFlonum?
	VALUE val = ((lir_folder2_t)folder)(LHS->Val, RHS->Val);
	return emit_load_const(builder, val);
    }
    return inst;
}

static lir_inst_t *remove_overflow_check(lir_builder_t *builder, lir_inst_t *inst)
{
    IFixnumAdd *ir = (IFixnumAdd *)inst;
    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumAdd:
	    return Emit_FixnumAdd(builder, ir->LHS, ir->RHS);
	case OPCODE_IFixnumSub:
	    return Emit_FixnumSub(builder, ir->LHS, ir->RHS);
	case OPCODE_IFixnumMul:
	    return Emit_FixnumMul(builder, ir->LHS, ir->RHS);
	case OPCODE_IFixnumDiv:
	    return Emit_FixnumDiv(builder, ir->LHS, ir->RHS);
	case OPCODE_IFixnumMod:
	    return Emit_FixnumMod(builder, ir->LHS, ir->RHS);
	default:
	    break;
    }
    return inst;
}

static lir_inst_t *fold_binop_cast(lir_builder_t *builder, lir_folder_t folder, lir_inst_t *inst)
{
    VALUE val = Qundef;
    ILoadConstObject *Val = (ILoadConstObject *)*lir_inst_get_args(inst, 0);
    int vopcode = lir_opcode(&Val->base);
    if (vopcode == OPCODE_IEnvStore) {
	Val = (ILoadConstObject *)((IEnvStore *)Val)->Val;
	vopcode = lir_opcode(&Val->base);
    }

    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumToFloat:
	case OPCODE_IFixnumToString:
	    if (vopcode == OPCODE_ILoadConstFixnum) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return emit_load_const(builder, val);
	    }
	    break;
	case OPCODE_IFloatToFixnum:
	case OPCODE_IFloatToString:
	    if (vopcode == OPCODE_ILoadConstFloat) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return emit_load_const(builder, val);
	    }
	    break;
	case OPCODE_IStringToFixnum:
	case OPCODE_IStringToFloat:
	    if (vopcode == OPCODE_ILoadConstFloat) {
		val = ((lir_folder1_t)folder)(Val->Val);
		return emit_load_const(builder, val);
	    }
	    break;
	case OPCODE_IObjectToString:
	    switch (vopcode) {
		case OPCODE_ILoadConstNil:
		case OPCODE_ILoadConstObject:
		case OPCODE_ILoadConstBoolean:
		case OPCODE_ILoadConstFixnum:
		case OPCODE_ILoadConstFloat:
		case OPCODE_ILoadConstString:
		case OPCODE_ILoadConstRegexp:
		    val = ((lir_folder1_t)folder)(Val->Val);
		    return emit_load_const(builder, val);
		default:
		    break;
	    }
	    break;
    }
    return inst;
}

static lir_inst_t *fold_binop_math(lir_builder_t *builder, lir_folder_t folder, lir_inst_t *inst)
{
    // (MathOp (LoadConstObject MathObj) (FloatValue))
    // => (LoadConstFloat)
    lir_inst_t *LHS = *lir_inst_get_args(inst, 0);
    lir_inst_t *RHS = *lir_inst_get_args(inst, 1);
    assert(folder == rb_jit_exec_IStringAdd);
    if (lir_opcode(LHS) == OPCODE_ILoadConstObject) {
	ILoadConstObject *left = (ILoadConstObject)LHS;
	if ((RBASIC_CLASS(left->Val) == rb_cMath) {
	    if (lir_opcode(RHS) == OPCODE_ILoadConstString) {
		ILoadConstString *rstr = (ILoadConstString *)RHS;
		VALUE val = rb_str_plus(lstr->Val, rstr->Val);
		lir_inst_t *tmp = emit_load_const(builder, val);
		return Emit_AllocString(builder, tmp);
	    }
	}
    }
    return inst;
}

static lir_inst_t *fold_string_add(lir_builder_t *builder, lir_folder_t folder, lir_inst_t *inst)
{
#if 1
    lir_inst_t *LHS = *lir_inst_get_args(inst, 0);
    lir_inst_t *RHS = *lir_inst_get_args(inst, 1);
    assert(folder == rb_jit_exec_IStringAdd);
    // (StringAdd (AllocStr LoadConstString1) LoadConstString2)
    // => (AllocStr LoadConstString3)
    if (lir_opcode(LHS) == OPCODE_IAllocString) {
	IAllocString *left = (IAllocString *)LHS;
	if (lir_opcode(left->OrigStr) == OPCODE_ILoadConstString) {
	    ILoadConstString *lstr = (ILoadConstString *)left->OrigStr;
	    if (lir_opcode(RHS) == OPCODE_ILoadConstString) {
		ILoadConstString *rstr = (ILoadConstString *)RHS;
		VALUE val = rb_str_plus(lstr->Val, rstr->Val);
		lir_inst_t *tmp = emit_load_const(builder, val);
		return Emit_AllocString(builder, tmp);
	    }
	}
    }
#endif
    return inst;
}

static lir_inst_t *constant_fold_inst(lir_builder_t *builder, lir_inst_t *inst)
{
    lir_folder_t folder;
    if (lir_is_guard(inst) || lir_is_terminator(inst)) {
	return inst;
    }
    if (is_constant(inst)) {
	return inst;
    }
    folder = const_fold_funcs[lir_opcode(inst)];
    if (folder == NULL) {
	return inst;
    }

    switch (lir_opcode(inst)) {
	case OPCODE_IFixnumToFloat:
	case OPCODE_IFixnumToString:
	case OPCODE_IFloatToFixnum:
	case OPCODE_IFloatToString:
	case OPCODE_IStringToFixnum:
	case OPCODE_IStringToFloat:
	case OPCODE_IObjectToString:
	    return fold_binop_cast(builder, folder, inst);

	//case OPCODE_IFixnumComplement :
	//case OPCODE_IStringLength :
	//case OPCODE_IStringEmptyP :
	//case OPCODE_IArrayLength :
	//case OPCODE_IArrayEmptyP :
	//case OPCODE_IArrayGet :
	//case OPCODE_IHashLength :
	//case OPCODE_IHashEmptyP :
	//case OPCODE_IHashGet :

	case OPCODE_IFixnumAdd:
	case OPCODE_IFixnumSub:
	case OPCODE_IFixnumMul:
	case OPCODE_IFixnumDiv:
	case OPCODE_IFixnumMod:
	case OPCODE_IFixnumAddOverflow:
	case OPCODE_IFixnumSubOverflow:
	case OPCODE_IFixnumMulOverflow:
	case OPCODE_IFixnumDivOverflow:
	case OPCODE_IFixnumModOverflow:
	case OPCODE_IFixnumEq:
	case OPCODE_IFixnumNe:
	case OPCODE_IFixnumGt:
	case OPCODE_IFixnumGe:
	case OPCODE_IFixnumLt:
	case OPCODE_IFixnumLe:
	case OPCODE_IFixnumAnd:
	case OPCODE_IFixnumOr:
	case OPCODE_IFixnumXor:
	case OPCODE_IFixnumLshift:
	case OPCODE_IFixnumRshift:
	    return fold_binop_fixnum2(builder, folder, inst);
	case OPCODE_IFloatAdd:
	case OPCODE_IFloatSub:
	case OPCODE_IFloatMul:
	case OPCODE_IFloatDiv:
	case OPCODE_IFloatMod:
	case OPCODE_IFloatEq:
	case OPCODE_IFloatNe:
	case OPCODE_IFloatGt:
	case OPCODE_IFloatGe:
	case OPCODE_IFloatLt:
	case OPCODE_IFloatLe:
	    return fold_binop_float2(builder, folder, inst);
	case OPCODE_IMathSin:
	case OPCODE_IMathCos:
	case OPCODE_IMathTan:
	case OPCODE_IMathExp:
	case OPCODE_IMathSqrt:
	case OPCODE_IMathLog10:
	case OPCODE_IMathLog2:
	    return fold_binop_math(builder, folder, inst);
	case OPCODE_IStringAdd:
	    return fold_string_add(builder, folder, inst);
	case OPCODE_IStringConcat:
	case OPCODE_IArrayConcat:
	case OPCODE_IRegExpMatch:
	    break;
	default:
	    break;
    }
    return inst;
}

typedef struct worklist_t {
    jit_list_t list;
    lir_builder_t *builder;
} worklist_t;

typedef int (*worklist_ir_func_t)(worklist_t *, lir_inst_t *);
typedef int (*worklist_bb_func_t)(worklist_t *, basicblock_t *);

static void worklist_push(worklist_t *list, lir_inst_t *ir)
{
    if (jit_list_indexof(&list->list, (uintptr_t)ir) == -1) {
	jit_list_add(&list->list, (uintptr_t)ir);
    }
}

static lir_inst_t *worklist_pop(worklist_t *list)
{
    lir_inst_t *last;
    assert(list->list.size > 0);
    last = (lir_t)jit_list_get(&list->list, list->list.size - 1);
    list->list.size -= 1;
    return last;
}

static int worklist_empty(worklist_t *list)
{
    return list->list.size == 0;
}

static void worklist_init(worklist_t *list, jit_list_t *blocks, lir_builder_t *builder)
{
    unsigned i, j;
    jit_list_init(&list->list);
    list->rec = rec;
    for (j = 0; j < blocks->size; j++) {
	basicblock_t *block = (basicblock_t *)jit_list_get(blocks, j);
	for (i = 0; i < block->insts.size; i++) {
	    lir_inst_t *inst = basicblock_get(block, i);
	    assert(inst != NULL);
	    worklist_push(list, inst);
	}
    }
}

static void worklist_init2(worklist_t *list, jit_list_t *blocks, lir_builder_t *builder)
{
    unsigned i;
    jit_list_init(&list->list);
    list->rec = rec;
    for (i = 0; i < lir_builder_blocks(rec)->size; i++) {
	basicblock_t *block = (basicblock_t *)jit_list_get(&rec->bblist, i);
	worklist_push(list, &block->base);
    }
}

static void worklist_dispose(worklist_t *list)
{
    jit_list_delete(&list->list);
}

static int apply_bb_worklist(lir_builder_t *builder, worklist_bb_func_t func)
{
    int modified = 0;
    worklist_t worklist;
    worklist_init2(&worklist, &rec->bblist, rec);
    while (!worklist_empty(&worklist)) {
	basicblock_t *bb = (basicblock_t *)worklist_pop(&worklist);
	modified += func(&worklist, bb);
    }
    worklist_dispose(&worklist);
    return modified;
}

static int apply_worklist(lir_builder_t *builder, worklist_ir_func_t func)
{
    int modified = 0;
    worklist_t worklist;
    worklist_init(&worklist, &rec->bblist, rec);
    while (!worklist_empty(&worklist)) {
	lir_inst_t *inst = worklist_pop(&worklist);
	modified += func(&worklist, inst);
    }
    worklist_dispose(&worklist);
    return modified;
}

static int constant_fold(worklist_t *list, lir_inst_t *inst)
{
    unsigned i;
    lir_inst_t *newinst;
    lir_builder_t *builder = list->rec;
    basicblock_t *prevBB = lir_builder_cur_bb(rec->builder);
    rec->cur_bb = inst->parent;
    newinst = constant_fold_inst(builder, inst);
    lir_builder_set_cur_bb(rec->builder, prevBB);
    if (inst != newinst) {
	if (inst->user) {
	    for (i = 0; i < inst->user->size; i++) {
		worklist_push(list, (lir_t)jit_list_get(inst->user, i));
	    }
	}
	lir_inst_replace_with(builder, inst, newinst);
	return 1;
    }
    return 0;
}

static int has_side_effect(lir_inst_t *inst)
{
    return lir_inst_have_side_effect(lir_opcode(inst));
}

static int need_by_side_exit(lir_builder_t *builder, lir_inst_t *inst)
{
    unsigned i, j, k;
    for (k = 0; k < lir_builder_blocks(rec)->size; k++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, k);
	for (j = 0; j < GET_STACK_MAP_ENTRY_SIZE(&bb->stack_map); j++) {
	    regstack_t *stack;
	    unsigned idx = GET_STACK_MAP_REAL_INDEX(j);
	    stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	    for (i = 0; i < stack->list.size; i++) {
		if (inst == regstack_get_direct(stack, i)) {
		    return 1;
		}
	    }
	}
    }
    return 0;
}

#define HAS_USER(INST) ((INST)->user && (INST)->user->size > 0)

static int inst_is_dead(lir_builder_t *builder, lir_inst_t *inst)
{
    if (HAS_USER(inst)) {
	return 0;
    }
    if (lir_is_terminator(inst)) {
	return 0;
    }
    if (lir_is_guard(inst)) {
	if (elimnate_guard(builder, inst)) {
	    return 1;
	}
	return 0;
    }
    if (has_side_effect(inst)) {
	return 0;
    }
    if (need_by_side_exit(builder, inst)) {
	return 0;
    }
    return 1;
}

static void remove_from_parent(lir_inst_t *inst)
{
    basicblock_t *bb = inst->parent;
    jit_list_remove(&bb->insts, (uintptr_t)inst);
    if (0 && DUMP_LIR > 0) {
	dump_lir_inst(inst);
    }
}

static void remove_from_side_exit(lir_builder_t *builder, lir_inst_t *inst)
{
    unsigned j, k;
    for (k = 0; k < lir_builder_blocks(rec)->size; k++) {
	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, k);
	for (j = 0; j < GET_STACK_MAP_ENTRY_SIZE(&bb->stack_map); j++) {
	    regstack_t *stack;
	    unsigned idx = GET_STACK_MAP_REAL_INDEX(j);
	    stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	    jit_list_remove(&stack->list, (uintptr_t)inst);
	}
    }
}

static void remove_side_exit(lir_builder_t *builder, lir_inst_t *inst)
{
    basicblock_t *bb = inst->parent;
    VALUE *pc = ((IGuardTypeFixnum *)inst)->Exit;
    int idx = jit_list_indexof(&bb->stack_map, (uintptr_t)pc);
    if (idx >= 0) {
	regstack_t *stack;
	VALUE *pc2;
	stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
	if (RC_DEC(stack) == 0) {
	    jit_list_remove_idx(&bb->stack_map, idx + 1);
	    if (JIT_DEBUG_VERBOSE) {
		fprintf(stderr, "delete side exit=%p pc=%p, inst_id=%d\n",
		        stack, pc, inst->id);
	    }
	    pc2 = (VALUE *)jit_list_remove_idx(&bb->stack_map, idx);
	    assert(pc == pc2);
	    assert(pc == stack->pc);
	}
    }
}

static int eliminate_dead_code(worklist_t *list, lir_inst_t *inst)
{
    int i = 0;
    lir_inst_t **ref = NULL;

    if (!inst_is_dead(list->rec, inst)) {
	return 0;
    }
    if (lir_is_guard(inst)) {
	remove_side_exit(list->rec, inst);
    }
    while ((ref = lir_inst_get_args(inst, i)) != NULL) {
	if (*ref) {
	    lir_inst_removeuser(*ref, inst);
	    worklist_push(list, *ref);
	}
	i += 1;
    }

    remove_from_parent(inst);
    return 1;
}

static int inst_combine(worklist_t *list, lir_inst_t *inst)
{
    unsigned opcode = inst->opcode;
    // naive method frame elimination
    if (opcode == OPCODE_IInvokeMethod || opcode == OPCODE_IInvokeBlock) {
	basicblock_t *bb = inst->parent;
	lir_t next = basicblock_get_next(bb, inst);
	if (next && next->opcode == OPCODE_IFramePop) {
	    remove_from_parent(next);
	    remove_from_parent(inst);
	    remove_from_side_exit(list->rec, inst);
	    return 1;
	}
    }
    if (LIR_OPT_INST_COMBINE_STACK_OP) {
	if (HAS_USER(inst)) {
	    return 0;
	}
	// operand stack (sp) operation elimination
	if (opcode == OPCODE_IStackPush) {
	    basicblock_t *bb = inst->parent;
	    unsigned i = basicblock_get_index(bb, inst) + 1;
	    for (; i < bb->insts.size; i++) {
		lir_t inst2 = basicblock_get(bb, i);
		if (lir_is_guard(inst2)) {
		    break;
		}
		if (inst2->opcode == OPCODE_IStackPush) {
		    break;
		}
		if (inst2->opcode == OPCODE_IStackPop) {
		    remove_from_parent(inst2);
		    remove_from_parent(inst);
		    remove_from_side_exit(list->rec, inst);
		    return 1;
		}
	    }
	}
	if (opcode == OPCODE_IStackPop) {
	    basicblock_t *bb = inst->parent;
	    unsigned next = basicblock_get_index(bb, inst) + 1;
	    lir_t inst2 = basicblock_get(bb, next);
	    if (inst2->opcode == OPCODE_IStackPush) {
		remove_from_parent(inst2);
		remove_from_parent(inst);
		remove_from_side_exit(list->rec, inst);
		return 1;
	    }
	}
    }
    return 0;
}

static int copy_propagation(worklist_t *list, basicblock_t *bb)
{
    unsigned i, j, k;
    int modified = 0;
    lir_builder_t *builder = list->rec;
    worklist_t worklist;

    jit_list_init(&worklist.list);
    worklist.rec = list->rec;

    for (i = 0; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	if (inst->opcode == OPCODE_ILoadSelf) {
	    for (j = i + 1; j < bb->insts.size; j++) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_ILoadSelf) {
		    lir_inst_replace_with(builder, inst2, inst);
		    worklist_push(&worklist, inst2);
		}
	    }
	}
	if (inst->opcode == OPCODE_IEnvLoad) {
	    for (j = i + 1; j < bb->insts.size; j++) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IEnvLoad) {
		    IEnvLoad *ir1 = (IEnvLoad *)inst;
		    IEnvLoad *ir2 = (IEnvLoad *)inst2;
		    if (ir1->Level == ir2->Level && ir1->Index == ir2->Index) {
			lir_inst_replace_with(builder, inst2, inst);
			worklist_push(&worklist, inst2);
		    }
		}
		if (inst2->opcode == OPCODE_IEnvStore) {
		    IEnvLoad *ir1 = (IEnvLoad *)inst;
		    IEnvStore *ir2 = (IEnvStore *)inst2;
		    if (ir1->Level == ir2->Level && ir1->Index == ir2->Index) {
			break;
		    }
		}
	    }
	}
	if (inst->opcode == OPCODE_IEnvStore) {
	    for (j = i + 1; j < bb->insts.size; j++) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IEnvStore) {
		    break;
		}
		if (inst2->opcode == OPCODE_IEnvLoad) {
		    IEnvStore *ir1 = (IEnvStore *)inst;
		    IEnvLoad *ir2 = (IEnvLoad *)inst2;
		    if (ir1->Level == ir2->Level && ir1->Index == ir2->Index) {
			lir_inst_replace_with(builder, inst2, inst);
			worklist_push(&worklist, inst2);
		    }
		}
	    }
	}
	if (inst->opcode == OPCODE_IGetPropertyName) {
	    for (j = i + 1; j < bb->insts.size; j++) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IGetPropertyName) {
		    IGetPropertyName *ir1 = (IGetPropertyName *)inst;
		    IGetPropertyName *ir2 = (IGetPropertyName *)inst2;
		    if (ir1->Recv == ir2->Recv && ir1->Index == ir2->Index) {
			lir_inst_replace_with(builder, inst2, inst);
			worklist_push(&worklist, inst2);
		    }
		}
		if (inst2->opcode == OPCODE_ISetPropertyName) {
		    IGetPropertyName *ir1 = (IGetPropertyName *)inst;
		    ISetPropertyName *ir2 = (ISetPropertyName *)inst2;
		    if (ir1->Recv == ir2->Recv && ir1->Index == ir2->Index) {
			break;
		    }
		}
	    }
	}
	if (inst->opcode == OPCODE_IInvokeMethod) {
	    IInvokeMethod *ir1 = (IInvokeMethod *)inst;
	    ISEQ iseq = ir1->ci->me->def->body.iseq;
	    for (k = 0; k < (unsigned)ir1->argc - 1; k++) {
		int index = iseq->local_size - k;
		lir_inst_t *argN = ir1->argv[k + 1];
		for (j = i + 1; j < bb->insts.size; j++) {
		    lir_inst_t *inst2 = basicblock_get(bb, j);
		    if (inst2->opcode == OPCODE_IEnvLoad) {
			IEnvLoad *ir2 = (IEnvLoad *)inst2;
			if (ir2->Level == 0 && ir2->Index == index) {
			    worklist_push(&worklist, inst2);
			    lir_inst_replace_with(builder, inst2, argN);
			    break;
			}
		    }
		    if (inst2->opcode == OPCODE_IEnvStore) {
			IEnvStore *ir2 = (IEnvStore *)inst2;
			if (ir2->Level == 0 && ir2->Index == index) {
			    break;
			}
		    }
		}
	    }
	}
    }

    while (!worklist_empty(&worklist)) {
	lir_inst_t *inst = worklist_pop(&worklist);
	remove_from_parent(inst);
	remove_side_exit(list->rec, inst);
	modified++;
    }
    worklist_dispose(&worklist);

    return modified;
}

static int dead_store_elimination(worklist_t *list, basicblock_t *bb)
{
    unsigned i, j;
    int modified = 0;
    lir_builder_t *builder = list->rec;
    worklist_t worklist;
    jit_list_init(&worklist.list);
    worklist.rec = list->rec;

    for (i = 1; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	if (inst->opcode == OPCODE_IEnvStore) {
	    for (j = i - 1; j != 0; j--) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IEnvLoad) {
		    break;
		}
		if (inst2->opcode == OPCODE_IEnvStore) {
		    IEnvStore *ir1 = (IEnvStore *)inst;
		    IEnvStore *ir2 = (IEnvStore *)inst2;
		    if (ir1->Level == ir2->Level && ir1->Index == ir2->Index) {
			lir_inst_replace_with(builder, inst2, ir2->Val);
			worklist_push(&worklist, inst2);
		    }
		}
	    }
	}
    }

    while (!worklist_empty(&worklist)) {
	lir_inst_t *inst = worklist_pop(&worklist);
	remove_from_parent(inst);
	remove_side_exit(list->rec, inst);
	modified++;
    }
    worklist_dispose(&worklist);

    return modified;
}

static int instruction_aggregation(worklist_t *list, basicblock_t *bb)
{
    unsigned i, j;
    int modified = 0;
    lir_builder_t *builder = list->rec;
    for (i = 1; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	/* load inst */
	if (inst->opcode == OPCODE_IEnvLoad) {
	    for (j = i - 1; j != 0; j--) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IEnvStore) {
		    IEnvLoad *ir1 = (IEnvLoad *)inst;
		    IEnvStore *ir2 = (IEnvStore *)inst2;
		    if (ir1->Level == ir2->Level && ir1->Index == ir2->Index) {
			break;
		    }
		}
		if (inst2->opcode == OPCODE_IEnvLoad) {
		    basicblock_insert_inst_after(bb, inst2, inst);
		    modified += 1;
		    break;
		}
	    }
	}
	if (inst->opcode == OPCODE_IGetPropertyName) {
	    IGetPropertyName *ir1 = (IGetPropertyName *)inst;
	    for (j = i - 1; j != 0; j--) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_ISetPropertyName) {
		    ISetPropertyName *ir2 = (ISetPropertyName *)inst2;
		    if (ir1->Recv == ir2->Recv && ir1->Index == ir2->Index) {
			break;
		    }
		}
		if (lir_is_guard(inst2)) {
		    break;
		}
		if (inst2->opcode == OPCODE_IGetPropertyName) {
		    basicblock_insert_inst_after(bb, inst2, inst);
		    modified += 1;
		    break;
		}
		if (inst2 == ir1->Recv) {
		    basicblock_insert_inst_after(bb, inst2, inst);
		    modified += 1;
		    break;
		}
	    }
	}
	/* guard inst */
	if (inst->opcode == OPCODE_IGuardMethodRedefine) {
	    IGuardMethodRedefine *ir1 = (IGuardMethodRedefine *)inst;
	    for (j = i - 1; j != 0; j--) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst2->opcode == OPCODE_IGuardMethodRedefine) {
		    IGuardMethodRedefine *ir2 = (IGuardMethodRedefine *)inst2;
		    remove_side_exit(builder, inst);
		    basicblock_insert_inst_after(bb, inst2, inst);
		    ir1->Exit = ir2->Exit;
		    update_side_exit_reference_count(inst);
		    modified += 1;
		    break;
		}
		// if (lir_is_guard(inst2)) {
		// }
	    }
	}
    }
    return modified;
}

static int is_same_type_guard(lir_t left, lir_t right)
{
    IGuardTypeSymbol *inst1 = (IGuardTypeSymbol *)left;
    IGuardTypeSymbol *inst2 = (IGuardTypeSymbol *)right;
    return inst1->R == inst2->R;
}

static int is_same_method_redefined_guard(lir_t left, lir_t right)
{
    IGuardMethodRedefine *inst1 = (IGuardMethodRedefine *)left;
    IGuardMethodRedefine *inst2 = (IGuardMethodRedefine *)right;
    return inst1->klass_flag == inst2->klass_flag && inst1->bop == inst2->bop;
}

static int is_same_call_info_guard(lir_t left, lir_t right)
{
    IGuardClassMethod *inst1 = (IGuardClassMethod *)left;
    IGuardClassMethod *inst2 = (IGuardClassMethod *)right;
    CALL_INFO ci1 = inst1->ci;
    CALL_INFO ci2 = inst2->ci;
    return inst1->R == inst2->R && ci1->mid == ci2->mid && ci1->class_serial == ci2->class_serial && ci1->method_state == ci2->method_state;
}

static int is_same_property_guard(lir_t left, lir_t right)
{
    IGuardProperty *inst1 = (IGuardProperty *)left;
    IGuardProperty *inst2 = (IGuardProperty *)right;
    if (inst1->R != inst2->R) {
	return 0;
    }
    if (inst1->is_attr != inst2->is_attr) {
	return 0;
    }
    return inst1->index == inst2->index && inst1->id == inst2->id;
}

static int remove_duplicated_guard(worklist_t *list, basicblock_t *bb)
{
    unsigned i, j;
    int modified = 0;
    worklist_t worklist;
    jit_list_init(&worklist.list);
    worklist.rec = list->rec;

    for (i = 1; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	if (lir_is_guard(inst)) {
	    for (j = i - 1; j != 0; j--) {
		lir_inst_t *inst2 = basicblock_get(bb, j);
		if (inst->opcode == inst2->opcode) {
		    switch (inst->opcode) {
			case OPCODE_IGuardTypeSymbol:
			case OPCODE_IGuardTypeFixnum:
			case OPCODE_IGuardTypeBignum:
			case OPCODE_IGuardTypeFloat:
			case OPCODE_IGuardTypeSpecialConst:
			case OPCODE_IGuardTypeArray:
			case OPCODE_IGuardTypeString:
			case OPCODE_IGuardTypeHash:
			case OPCODE_IGuardTypeRegexp:
			case OPCODE_IGuardTypeTime:
			case OPCODE_IGuardTypeMath:
			case OPCODE_IGuardTypeObject:
			    if (is_same_type_guard(inst, inst2)) {
				worklist_push(&worklist, inst);
			    }
			    break;
			case OPCODE_IGuardMethodRedefine:
			    if (is_same_method_redefined_guard(inst, inst2)) {
				worklist_push(&worklist, inst);
			    }
			    break;
			case OPCODE_IGuardClassMethod:
			case OPCODE_IGuardMethodCache:
			    if (is_same_call_info_guard(inst, inst2)) {
				worklist_push(&worklist, inst);
			    }
			    break;
			case OPCODE_IGuardProperty:
			    if (is_same_property_guard(inst, inst2)) {
				worklist_push(&worklist, inst);
			    }
			    break;
		    }
		}
	    }
	}
    }
    while (!worklist_empty(&worklist)) {
	lir_inst_t *inst = worklist_pop(&worklist);
	remove_from_parent(inst);
	remove_side_exit(list->rec, inst);
	modified++;
    }
    worklist_dispose(&worklist);

    return modified;
}

static lir_t trace_recorder_create_undef(lir_builder_t *builder, basicblock_t *bb)
{
    lir_t reg;
    basicblock_t *prev = lir_builder_cur_bb(rec->builder);
    unsigned inst_size = bb->insts.size;
    rec->cur_bb = bb;
    reg = Emit_Undef(rec);
    if (inst_size > 0 && inst_size != basicblock_size(bb)) {
	basicblock_swap_inst(bb, inst_size - 1, inst_size);
    }
    lir_builder_set_cur_bb(rec->builder, prev);
    return reg;
}

static lir_t trace_recorder_create_load(lir_builder_t *builder, basicblock_t *bb, int idx, int lev)
{
    lir_t reg;
    basicblock_t *prev = lir_builder_cur_bb(rec->builder);
    unsigned inst_size = bb->insts.size;
    rec->cur_bb = bb;
    if (idx == 0 && lev == 0) {
	reg = Emit_LoadSelf(rec);
    }
    else {
	reg = Emit_EnvLoad(builder, lev, idx);
    }
    if (inst_size > 0 && inst_size != basicblock_size(bb)) {
	basicblock_swap_inst(bb, inst_size - 1, inst_size);
    }
    lir_builder_set_cur_bb(rec->builder, prev);
    return reg;
}

static lir_t trace_recorder_create_phi(lir_builder_t *builder, basicblock_t *bb, int argc, lir_t *argv)
{
    lir_t reg;
    basicblock_t *prev = lir_builder_cur_bb(rec->builder);
    unsigned inst_size = bb->insts.size;
    rec->cur_bb = bb;
    reg = Emit_Phi(builder, argc, argv);
    if (inst_size > 0 && inst_size != basicblock_size(bb)) {
	lir_t first_inst = basicblock_get(bb, 0);
	if (first_inst->opcode == OPCODE_IInvokeMethod || first_inst->opcode == OPCODE_IInvokeBlock) {
	    first_inst = basicblock_get(bb, 1);
	}
	basicblock_insert_inst_before(bb, first_inst, reg);
    }
    lir_builder_set_cur_bb(rec->builder, prev);
    return reg;
}

static void trace_recorder_replace_phi_argument(lir_builder_t *builder, IPhi *phi, int idx, lir_t reg)
{
    lir_inst_removeuser(phi->argv[idx], (lir_t)phi);
    lir_inst_adduser(builder, reg, (lir_t)phi);
    phi->argv[idx] = reg;
}

/*
 * This pass insert PHI instruction that s a pseudo code to reveal
 * variables that are loop-variant expression.
 */
static int insert_phi(worklist_t *list, basicblock_t *bb)
{
    unsigned i, j;
    struct variable_table_iterator itr;
    variable_table_t *cur_vt = bb->init_table;
    variable_table_t *newvt;
    lir_builder_t *builder = list->rec;
    if (bb->preds.size == 1 /*&& variable_table_equal(prev->vt, cur_vt) */) {
	return 0;
    }
    for (i = 0; i < bb->preds.size; i++) {
	basicblock_t *pred = basicblock_get_pred(bb, i);
	variable_table_t *pred_vt = pred->last_table;
	if (variable_table_depth(cur_vt) != variable_table_depth(pred_vt)) {
	    return 0;
	}
    }
    newvt = variable_table_clone(&rec->mpool, cur_vt);
    for (i = 0; i < bb->preds.size; i++) {
	basicblock_t *pred = basicblock_get_pred(bb, i);
	variable_table_t *pred_vt = pred->last_table;
	variable_table_iterator_init(pred_vt, &itr, 0);
	while (variable_table_each(&itr)) {
	    if (variable_table_get(newvt, itr.idx, itr.lev, 0) == NULL) {
		lir_t phi;
		lir_t undef = trace_recorder_create_undef(builder, bb);
		lir_t argv[bb->preds.size];
		for (j = 0; j < bb->preds.size; j++) {
		    argv[j] = undef;
		}
		phi = trace_recorder_create_phi(builder, bb, bb->preds.size, argv);
		if (itr.idx == 0 && itr.lev == 0) {
		    variable_table_set_self(newvt, 0, phi);
		}
		else {
		    variable_table_set(newvt, itr.idx, itr.lev, 0, phi);
		}
	    }
	}
    }
    for (i = 0; i < bb->preds.size; i++) {
	basicblock_t *pred = basicblock_get_pred(bb, i);
	variable_table_t *pred_vt = pred->last_table;
	variable_table_iterator_init(newvt, &itr, 0);
	while (variable_table_each(&itr)) {
	    IPhi *phi = (IPhi *)itr.val;
	    lir_t reg = NULL;
	    assert(itr.val->opcode == OPCODE_IPhi);
	    reg = variable_table_get(pred_vt, itr.idx, itr.lev, 0);
	    if (reg == NULL && itr.idx == 0 && itr.lev == 0) {
		reg = pred_vt->self;
	    }
	    if (reg == NULL) {
		reg = trace_recorder_create_load(builder, pred, itr.idx, itr.lev);
	    }
	    trace_recorder_replace_phi_argument(builder, phi, i, reg);
	}
    }
    bb->init_table = newvt;
    return 0;
}

static void loop_invariant_code_motion(lir_builder_t *builder, int move_block)
{
    unsigned i, j, k;
    lir_t *ref = NULL;
    for (j = 0; j < lir_builder_blocks(rec)->size; j++) {
	basicblock_t *bb = lir_builder_get_block(rec->builder, j);
	for (i = 0; i < bb->insts.size; i++) {
	    lir_inst_t *inst = basicblock_get(bb, i);
	    if (inst->opcode == OPCODE_IPhi) {
		k = 0;
		while ((ref = lir_inst_get_args(inst, k)) != NULL) {
		    (*ref)->flag |= LIR_INST_VARIANT;
		    k++;
		}
	    }
	    // if (inst->opcode == OPCODE_ILoadEnv ||
	    //  inst->opcode == OPCODE_ILoadEnv) {
	    // }
	}
    }
}

static int simplify_cfg(worklist_t *list, basicblock_t *bb)
{
    /* Fold Control Flow
     * [Case1]
     *     [Before]            [After]
     * BB0: FixnumAdd $a $b | BB0: FixnumAdd $a $b
     *      ...             |      ...
     *      IJump BB1       |      IJump BB2
     * BB2: IJump BB2       | BB2: FixnumAdd $c $d
     * BB1: FixnumAdd $c $d |      ...
     *      ...             |
     */

    lir_t inst = basicblock_get_terminator(bb);
    if (bb->insts.size == 1 && inst->opcode == OPCODE_IJump) {
	if (bb->preds.size == 1 && bb->succs.size == 1) {
	    basicblock_t *succ = basicblock_get_succ(bb, 0);
	    basicblock_t *pred = basicblock_get_pred(bb, 0);
	    inst = basicblock_get_terminator(pred);
	    assert(inst->opcode == OPCODE_IJump);
	    ((IJump *)inst)->TargetBB = succ;
	    basicblock_unlink(pred, bb);
	    basicblock_unlink(bb, succ);
	    basicblock_link(pred, succ);
	    trace_recorder_remove_bb(list->rec, bb);
	    return 1;
	}
    }

    if (bb->succs.size == 1) {
	basicblock_t *succ = basicblock_get_succ(bb, 0);
	if ((succ->base.flag & BB_PUSH_FRAME) || (succ->base.flag & BB_POP_FRAME)) {
	    return 0;
	}
	if (bb->base.flag & BB_POP_FRAME) {
	    return 0;
	}
	if (succ->preds.size == 1) {
	    unsigned i;
	    variable_table_t *vt;
	    assert(inst->opcode == OPCODE_IJump);
	    remove_from_parent(inst);
	    for (i = 0; i < succ->insts.size; i++) {
		basicblock_append(bb, basicblock_get(succ, i));
	    }
	    basicblock_unlink(bb, succ);
	    if (succ->succs.size > 0) {
		basicblock_t *succ2 = basicblock_get_succ(succ, 0);
		assert(succ->succs.size == 1);
		basicblock_unlink(succ, succ2);
		basicblock_link(bb, succ2);
	    }
	    /* swap vt */
	    vt = bb->last_table;
	    bb->last_table = succ->last_table;
	    succ->last_table = vt;

	    /* copy stack_map */
	    for (i = 0; i < succ->stack_map.size; i += 2) {
		uintptr_t pc = jit_list_get(&succ->stack_map, i + 0);
		uintptr_t stack = jit_list_get(&succ->stack_map, i + 1);
		jit_list_add(&bb->stack_map, pc);
		jit_list_add(&bb->stack_map, stack);
	    }
	    if (succ->base.flag & BB_PUSH_FRAME) {
		bb->base.flag |= BB_PUSH_FRAME;
	    }
	    if (succ->base.flag & BB_POP_FRAME) {
		bb->base.flag |= BB_POP_FRAME;
	    }
	    succ->insts.size = 0;
	    trace_recorder_remove_bb(list->rec, succ);
	    return 1;
	}
    }
    return 0;
}

static int check_side_exit_reference_count(worklist_t *list, basicblock_t *bb)
{
    unsigned i;
    for (i = 0; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	if (lir_is_guard(inst) || inst->opcode == OPCODE_IExit) {
	    update_side_exit_reference_count(inst);
	}
    }
    return 0;
}

static int check_basicblock_flag(worklist_t *list, basicblock_t *bb)
{
    unsigned i;
    for (i = 0; i < bb->insts.size; i++) {
	lir_inst_t *inst = basicblock_get(bb, i);
	if (inst->opcode == OPCODE_IInvokeBlock || inst->opcode == OPCODE_IInvokeMethod || inst->opcode == OPCODE_IInvokeConstructor) {
	    bb->base.flag |= BB_PUSH_FRAME;
	}
	if (inst->opcode == OPCODE_IFramePop) {
	    bb->base.flag |= BB_POP_FRAME;
	}
    }
    return 0;
}

static int check_basicblock_link(worklist_t *list, basicblock_t *bb)
{
    lir_t inst = basicblock_get_terminator(bb);
    if (inst->opcode == OPCODE_IJump) {
	basicblock_t *target = ((IJump *)inst)->TargetBB;
	basicblock_link(bb, target);
    }
    return 0;
}

static void trace_optimize(lir_builder_t *builder, trace_t *trace)
{
    int modified = 1;
    JIT_PROFILE_ENTER("optimize");
    apply_bb_worklist(builder, check_side_exit_reference_count);
    apply_bb_worklist(builder, check_basicblock_link);
    apply_bb_worklist(builder, check_basicblock_flag);
    apply_bb_worklist(builder, remove_duplicated_guard);
    while (modified) {
	modified = 0;
	if (LIR_OPT_CONSTANT_FOLDING) {
	    modified += apply_worklist(builder, constant_fold);
	}
	if (LIR_OPT_DEAD_CODE_ELIMINATION) {
	    modified += apply_bb_worklist(builder, copy_propagation);
	    modified += apply_bb_worklist(builder, dead_store_elimination);
	    modified += apply_worklist(builder, eliminate_dead_code);
	}
	if (LIR_OPT_INST_COMBINE) {
	    modified += apply_worklist(builder, inst_combine);
	    modified += apply_bb_worklist(builder, simplify_cfg);
	}

	if (modified) {
	    modified += apply_bb_worklist(builder, instruction_aggregation);
	    modified += apply_bb_worklist(builder, remove_duplicated_guard);
	}
    }
    if (LIR_OPT_LOOP_INVARIANT_CODE_MOTION) {
	apply_bb_worklist(builder, insert_phi);
	loop_invariant_code_motion(builder, LIR_OPT_BLOCK_GUARD_MOTION);
    }
    if (LIR_OPT_ESCAPE_ANALYSIS) {
	// escape_analysis(rec);
	// stack_allocation(rec);
    }
    if (LIR_OPT_RANGE_ANALYSIS) {
	// range_analysis(rec);
	if (LIR_OPT_DEAD_CODE_ELIMINATION) {
	    apply_worklist(builder, eliminate_dead_code);
	}
    }
    if (LIR_OPT_PEEPHOLE_OPTIMIZATION) {
	// apply_worklist(builder, peephole);
    }
    JIT_PROFILE_LEAVE("optimize", JIT_DUMP_COMPILE_LOG > 0);
}
