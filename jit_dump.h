static void dump_inst(jit_event_t *e)
{
    if (DUMP_INST > 0) {
	long pc = (e->pc - e->cfp->iseq->iseq_encoded);
	fprintf(stderr, "%04ld pc=%p %02d %s\n",
	        pc, e->pc, e->opcode, insn_name(e->opcode));
    }
}

static void dump_lir_inst(lir_t inst)
{
    if (DUMP_LIR > 0) {
	switch (inst->opcode) {
#define DUMP_IR(OPNAME)      \
    case OPCODE_I##OPNAME:   \
	Dump_##OPNAME(inst); \
	break;
	    LIR_EACH(DUMP_IR);
	    default:
		assert(0 && "unreachable");
#undef DUMP_IR
	}
	if (0) {
	    fprintf(stderr, "user=[");
	    if (inst->user) {
		unsigned i;
		for (i = 0; i < inst->user->size; i++) {
		    lir_t ir = (lir_t)jit_list_get(inst->user, i);
		    if (i != 0) {
			fprintf(stderr, ",");
		    }
		    fprintf(stderr, "v%02u", ir->id);
		}
	    }
	    fprintf(stderr, "]\n");
	}
    }
}

static void dump_lir_block(basicblock_t *block)
{
    if (DUMP_LIR > 0) {
	unsigned i = 0;
	fprintf(stderr, "BB%d (pc=%p)", lir_getid(&block->base), block->pc);

	if (block->preds.size > 0) {
	    fprintf(stderr, " pred=[");
	    for (i = 0; i < block->preds.size; i++) {
		basicblock_t *bb = (basicblock_t *)block->preds.list[i];
		if (i != 0)
		    fprintf(stderr, ",");
		fprintf(stderr, "BB%d", lir_getid(&bb->base));
	    }
	    fprintf(stderr, "]");
	}

	if (block->succs.size > 0) {
	    fprintf(stderr, " succ=[");
	    for (i = 0; i < block->succs.size; i++) {
		basicblock_t *bb = (basicblock_t *)block->succs.list[i];
		if (i != 0)
		    fprintf(stderr, ",");
		fprintf(stderr, "BB%d", lir_getid(&bb->base));
	    }
	    fprintf(stderr, "]");
	}

	fprintf(stderr, "\n");
	//variable_table_dump(block->init_table);
	//variable_table_dump(block->last_table);
	for (i = 0; i < block->insts.size; i++) {
	    lir_t inst = (lir_t)block->insts.list[i];
	    dump_lir_inst(inst);
	}
    }
}

//static const char *trace_status_to_str(trace_exit_status_t reason)
//{
//    switch (reason) {
//	case TRACE_EXIT_ERROR:
//	    return "TRACE_EXIT_ERROR";
//	case TRACE_EXIT_SUCCESS:
//	    return "TRACE_EXIT_SUCCESS";
//	case TRACE_EXIT_SIDE_EXIT:
//	    return "TRACE_EXIT_SIDE_EXIT";
//    }
//    return "-1";
//}
//
//#define GET_STACK_MAP_ENTRY_SIZE(MAP) ((MAP)->size / 2)
//#define GET_STACK_MAP_REAL_INDEX(IDX) ((IDX)*2)
//static void dump_side_exit(trace_recorder_t *rec)
//{
//    if (DUMP_LIR > 0) {
//	unsigned i, j, k;
//	for (k = 0; k < rec->bblist.size; k++) {
//	    basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, k);
//	    for (j = 0; j < GET_STACK_MAP_ENTRY_SIZE(&bb->stack_map); j++) {
//		unsigned idx = GET_STACK_MAP_REAL_INDEX(j);
//		VALUE *pc = (VALUE *)jit_list_get(&bb->stack_map, idx);
//		regstack_t *stack = (regstack_t *)jit_list_get(&bb->stack_map, idx + 1);
//		fprintf(stderr, "side exit %s (size=%04d, refc=%ld): pc=%p: ",
//		        trace_status_to_str(stack->flag),
//		        stack->list.size - LIR_RESERVED_REGSTACK_SIZE,
//		        stack->refc,
//		        pc);
//		for (i = 0; i < stack->list.size; i++) {
//		    lir_t inst = (lir_t)jit_list_get(&stack->list, i);
//		    if (inst) {
//			fprintf(stderr, "  [%d] = %04ld;", i - LIR_RESERVED_REGSTACK_SIZE, lir_getid(inst));
//		    }
//		}
//		fprintf(stderr, "\n");
//	    }
//	}
//    }
//}

void dump_lir_func(lir_func_t *func)
{
    if (DUMP_LIR > 0) {
	unsigned i;
	fprintf(stderr, "---------------\n");
	fprintf(stderr, "func %p id=%u\n", func, func->id);
	fprintf(stderr, "  bbsize=%u\n", jit_list_size(&func->bblist));

	fprintf(stderr, "---------------\n");
	for (i = 0; i < jit_list_size(&func->bblist); i++) {
	    basicblock_t *bb = JIT_LIST_GET(basicblock_t *, &func->bblist, i);
	    fprintf(stderr, "---------------\n");
	    dump_lir_block(bb);
	}
	fprintf(stderr, "---------------\n");
	//dump_side_exit(rec);
	//fprintf(stderr, "---------------\n");
    }
}
