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
		    lir_t ir = JIT_LIST_GET(lir_t, inst->user, i);
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
	fprintf(stderr, "  ");
	fprintf(stderr, "%d ", (block->base.flag & (1 << 0)) == (1 << 0));
	fprintf(stderr, "%d ", (block->base.flag & (1 << 1)) == (1 << 1));
	fprintf(stderr, "\n");

	fprintf(stderr, " pred=[");
	if (block->preds.size > 0) {
	    for (i = 0; i < block->preds.size; i++) {
		basicblock_t *bb = (basicblock_t *)block->preds.list[i];
		if (i != 0)
		    fprintf(stderr, ",");
		fprintf(stderr, "BB%d", lir_getid(&bb->base));
	    }
	}
	fprintf(stderr, "]");

	fprintf(stderr, " succ=[");
	if (block->succs.size > 0) {
	    for (i = 0; i < block->succs.size; i++) {
		basicblock_t *bb = (basicblock_t *)block->succs.list[i];
		if (i != 0)
		    fprintf(stderr, ",");
		fprintf(stderr, "BB%d", lir_getid(&bb->base));
	    }
	}
	fprintf(stderr, "]");

	fprintf(stderr, "\n");
	fprintf(stderr, "init:");
	local_var_table_dump(block->init_table);
	fprintf(stderr, "last:");
	local_var_table_dump(block->last_table);
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

static void dump_side_exit(lir_func_t *func)
{
    if (DUMP_LIR > 0) {
	unsigned i, j;
	jit_list_t *bblist = &func->bblist;
	for (i = 0; i < jit_list_size(bblist); i++) {
	    basicblock_t *bb = JIT_LIST_GET(basicblock_t *, bblist, i);
	    for (j = 0; j < jit_list_size(&bb->side_exits); j++) {
		jit_snapshot_t *snapshot;
		snapshot = JIT_LIST_GET(jit_snapshot_t *, &bb->side_exits, j);
		fprintf(stderr, "bb%d ", basicblock_id(bb));
		jit_snapshot_dump(snapshot);
	    }
	}
    }
}

void dump_lir_func(lir_func_t *func)
{
    if (DUMP_LIR > 0) {
	unsigned i;
	fprintf(stderr, "---------------\n");
	fprintf(stderr, "func %p id=%u\n", func, func->id);
	fprintf(stderr, "  bbsize=%u\n", jit_list_size(&func->bblist));

	for (i = 0; i < jit_list_size(&func->bblist); i++) {
	    basicblock_t *bb = JIT_LIST_GET(basicblock_t *, &func->bblist, i);
	    fprintf(stderr, "---------------\n");
	    dump_lir_block(bb);
	}
	fprintf(stderr, "---------------\n");
	dump_side_exit(func);
	fprintf(stderr, "---------------\n");
    }
}

void dump_lir_builder(lir_builder_t *builder)
{
    if (DUMP_LIR > 0) {
	unsigned i;
	for (i = 0; i < jit_list_size(&builder->func_list); i++) {
	    lir_func_t *func = JIT_LIST_GET(lir_func_t *, &builder->func_list, i);
	    dump_lir_func(func);
	}
    }
}
