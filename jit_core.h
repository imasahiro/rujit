///**********************************************************************
//
//  jit_core.c -
//
//  $Author$
//
//  Copyright (C) 2014 Masahiro Ide
//
//**********************************************************************/
//static lir_t trace_recorder_add_inst(trace_recorder_t *recorder, lir_t inst, unsigned inst_size);
//
///* const pool { */
//#define CONST_POOL_INIT_SIZE 1
//static const_pool_t *const_pool_init(const_pool_t *self)
//{
//    jit_list_init(&self->list);
//    jit_list_init(&self->inst);
//    return self;
//}
//
//static int const_pool_contain(const_pool_t *self, const void *ptr)
//{
//    return jit_list_indexof(&self->list, (uintptr_t)ptr);
//}
//
//static int const_pool_add(const_pool_t *self, const void *ptr, lir_t val)
//{
//    int idx;
//    if ((idx = const_pool_contain(self, ptr)) != -1) {
//	return idx;
//    }
//    jit_list_add(&self->list, (uintptr_t)ptr);
//    jit_list_add(&self->inst, (uintptr_t)val);
//    return self->list.size - 1;
//}
//
//static void const_pool_delete(const_pool_t *self)
//{
//    jit_list_delete(&self->list);
//    jit_list_delete(&self->inst);
//    if (JIT_DEBUG_VERBOSE >= 10) {
//	memset(self, 0, sizeof(*self));
//    }
//}
//
///* } const pool */
//
///* variable_table { */
//struct variable_table {
//    jit_list_t table;
//    lir_t self;
//    lir_t first_inst;
//    variable_table_t *next;
//};
//
//struct variable_table_iterator {
//    variable_table_t *vt;
//    int off;
//    unsigned idx;
//    unsigned lev;
//    unsigned nest_level;
//    lir_t val;
//};
//
//static variable_table_t *variable_table_init(struct memory_pool *mp, lir_t inst)
//{
//    variable_table_t *vt;
//    vt = (variable_table_t *)memory_pool_alloc(mp, sizeof(*vt));
//    vt->first_inst = inst;
//    vt->self = NULL;
//    vt->next = NULL;
//    return vt;
//}
//
//static void variable_table_set_self(variable_table_t *vt, unsigned nest_level, lir_t reg)
//{
//    while (nest_level-- != 0) {
//	vt = vt->next;
//    }
//    assert(vt != NULL);
//    vt->self = reg;
//}
//
//static lir_t variable_table_get_self(variable_table_t *vt, unsigned nest_level)
//{
//    while (nest_level-- != 0) {
//	vt = vt->next;
//    }
//    assert(vt != NULL);
//    return vt->self;
//}
//
//static void variable_table_set(variable_table_t *vt, unsigned idx, unsigned lev, unsigned nest_level, lir_t reg)
//{
//    unsigned i;
//    while (nest_level-- != 0) {
//	vt = vt->next;
//    }
//    assert(vt != NULL);
//    for (i = 0; i < vt->table.size; i += 3) {
//	if (jit_list_get(&vt->table, i) == idx) {
//	    if (jit_list_get(&vt->table, i + 1) == lev) {
//		jit_list_set(&vt->table, i + 2, (uintptr_t)reg);
//		return;
//	    }
//	}
//    }
//    jit_list_add(&vt->table, idx);
//    jit_list_add(&vt->table, lev);
//    jit_list_add(&vt->table, (uintptr_t)reg);
//}
//
//static lir_t variable_table_get(variable_table_t *vt, unsigned idx, unsigned lev, unsigned nest_level)
//{
//    unsigned i;
//    while (nest_level-- != 0) {
//	vt = vt->next;
//    }
//    assert(vt != NULL);
//    for (i = 0; i < vt->table.size; i += 3) {
//	if (jit_list_get(&vt->table, i) == idx) {
//	    if (jit_list_get(&vt->table, i + 1) == lev) {
//		return (lir_t)jit_list_get(&vt->table, i + 2);
//	    }
//	}
//    }
//    return NULL;
//}
//
//static variable_table_t *variable_table_clone_once(struct memory_pool *mp, variable_table_t *vt)
//{
//    unsigned i;
//    variable_table_t *newvt = variable_table_init(mp, vt->first_inst);
//    newvt->self = vt->self;
//    for (i = 0; i < vt->table.size; i++) {
//	uintptr_t val = jit_list_get(&vt->table, i);
//	jit_list_add(&newvt->table, (uintptr_t)val);
//    }
//    return newvt;
//}
//
//static variable_table_t *variable_table_clone(struct memory_pool *mp, variable_table_t *vt)
//{
//    variable_table_t *newvt = variable_table_clone_once(mp, vt);
//    variable_table_t *root = newvt;
//    vt = vt->next;
//    while (vt) {
//	newvt->next = variable_table_clone_once(mp, vt);
//	vt = vt->next;
//	newvt = newvt->next;
//    }
//    return root;
//}
//
//static void variable_table_delete(variable_table_t *vt)
//{
//    while (vt) {
//	jit_list_delete(&vt->table);
//	vt = vt->next;
//    }
//}
//
//static inline long lir_getid(lir_t ir);
//static void variable_table_dump(variable_table_t *vt)
//{
//    while (vt) {
//	unsigned i;
//	fprintf(stderr, "[");
//	if (vt->self) {
//	    fprintf(stderr, "self=v%ld", lir_getid(vt->self));
//	}
//	for (i = 0; i < vt->table.size; i += 3) {
//	    uintptr_t idx = jit_list_get(&vt->table, i + 0);
//	    uintptr_t lev = jit_list_get(&vt->table, i + 1);
//	    uintptr_t reg = jit_list_get(&vt->table, i + 2);
//	    if ((vt->self && i == 0) || i != 0) {
//		fprintf(stderr, ",");
//	    }
//	    fprintf(stderr, "(%lu, %lu)=v%ld", lev, idx, lir_getid(((lir_t)reg)));
//	}
//	fprintf(stderr, "]");
//	if (vt->next) {
//	    fprintf(stderr, "->");
//	}
//	vt = vt->next;
//    }
//    fprintf(stderr, "\n");
//}
//
//static unsigned variable_table_depth(variable_table_t *vt)
//{
//    unsigned depth = 0;
//    while (vt) {
//	vt = vt->next;
//	depth++;
//    }
//    return depth;
//}
//
//static void variable_table_iterator_init(variable_table_t *vt, struct variable_table_iterator *itr, unsigned nest_level)
//{
//    itr->vt = vt;
//    itr->nest_level = nest_level;
//    itr->off = -1;
//    itr->idx = 0;
//    itr->lev = 0;
//    itr->val = NULL;
//}
//
//static int variable_table_each(struct variable_table_iterator *itr)
//{
//    // 1. self
//    if (itr->off == -1) {
//	itr->off = 0;
//	itr->idx = 0;
//	itr->lev = 0;
//	if (itr->vt->self) {
//	    itr->val = itr->vt->self;
//	    return 1;
//	}
//    }
//    // 2. env
//    if (itr->off < (int)itr->vt->table.size) {
//	itr->idx = (int)jit_list_get(&itr->vt->table, itr->off + 0);
//	itr->lev = (int)jit_list_get(&itr->vt->table, itr->off + 1);
//	itr->val = (lir_t)jit_list_get(&itr->vt->table, itr->off + 2);
//	itr->off += 3;
//	return 1;
//    }
//    return 0;
//}
//
//static void trace_recorder_push_variable_table(trace_recorder_t *rec, lir_t inst)
//{
//    struct memory_pool *mp = &rec->mpool;
//    variable_table_t *vt = lir_builder_cur_bb(rec->builder)->last_table;
//    lir_builder_cur_bb(rec->builder)->last_table = variable_table_init(mp, inst);
//    lir_builder_cur_bb(rec->builder)->last_table->next = vt;
//    assert(lir_builder_cur_bb(rec->builder)->last_table != NULL);
//}
//
//static void trace_recorder_insert_vtable(trace_recorder_t *rec, unsigned level)
//{
//    unsigned i;
//    for (i = 0; i < rec->bblist.size; i++) {
//	basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
//	while (variable_table_depth(bb->init_table) <= level) {
//	    variable_table_t *tail = bb->init_table;
//	    while (tail->next != NULL) {
//		tail = tail->next;
//	    }
//	    tail->next = variable_table_init(&rec->mpool, NULL);
//	}
//	while (variable_table_depth(bb->last_table) <= level) {
//	    variable_table_t *tail = bb->last_table;
//	    while (tail->next != NULL) {
//		tail = tail->next;
//	    }
//	    tail->next = variable_table_init(&rec->mpool, NULL);
//	}
//    }
//}
//
//static variable_table_t *trace_recorder_pop_variable_table(trace_recorder_t *rec)
//{
//    variable_table_t *vt = lir_builder_cur_bb(rec->builder)->last_table;
//
//    if (variable_table_depth(vt) == 1) {
//	trace_recorder_insert_vtable(rec, 1);
//	vt = lir_builder_cur_bb(rec->builder)->last_table;
//    }
//    lir_builder_cur_bb(rec->builder)->last_table = vt->next;
//    assert(lir_builder_cur_bb(rec->builder)->last_table != NULL);
//    return vt;
//}
///* } variable_table */
//
//static void dump_inst(jit_event_t *e)
//{
//    if (DUMP_INST > 0) {
//	long pc = (e->pc - e->cfp->iseq->iseq_encoded);
//	fprintf(stderr, "%04ld pc=%p %02d %s\n",
//	        pc, e->pc, e->opcode, insn_name(e->opcode));
//    }
//}
//
//static void dump_lir_inst(lir_t inst)
//{
//    if (DUMP_LIR > 0) {
//	switch (inst->opcode) {
//#define DUMP_IR(OPNAME)      \
//    case OPCODE_I##OPNAME:   \
//	Dump_##OPNAME(inst); \
//	break;
//	    LIR_EACH(DUMP_IR);
//	    default:
//		assert(0 && "unreachable");
//#undef DUMP_IR
//	}
//	if (0) {
//	    fprintf(stderr, "user=[");
//	    if (inst->user) {
//		unsigned i;
//		for (i = 0; i < inst->user->size; i++) {
//		    lir_t ir = (lir_t)jit_list_get(inst->user, i);
//		    if (i != 0) {
//			fprintf(stderr, ",");
//		    }
//		    fprintf(stderr, "v%02u", ir->id);
//		}
//	    }
//	    fprintf(stderr, "]\n");
//	}
//    }
//}
//
//static void dump_lir_block(basicblock_t *block)
//{
//    if (DUMP_LIR > 0) {
//	unsigned i = 0;
//	fprintf(stderr, "BB%ld (pc=%p)", lir_getid(&block->base), block->start_pc);
//
//	if (block->preds.size > 0) {
//	    fprintf(stderr, " pred=[");
//	    for (i = 0; i < block->preds.size; i++) {
//		basicblock_t *bb = (basicblock_t *)block->preds.list[i];
//		if (i != 0)
//		    fprintf(stderr, ",");
//		fprintf(stderr, "BB%ld", lir_getid(&bb->base));
//	    }
//	    fprintf(stderr, "]");
//	}
//
//	if (block->succs.size > 0) {
//	    fprintf(stderr, " succ=[");
//	    for (i = 0; i < block->succs.size; i++) {
//		basicblock_t *bb = (basicblock_t *)block->succs.list[i];
//		if (i != 0)
//		    fprintf(stderr, ",");
//		fprintf(stderr, "BB%ld", lir_getid(&bb->base));
//	    }
//	    fprintf(stderr, "]");
//	}
//
//	fprintf(stderr, "\n");
//	variable_table_dump(block->init_table);
//	variable_table_dump(block->last_table);
//	for (i = 0; i < block->insts.size; i++) {
//	    lir_t inst = (lir_t)block->insts.list[i];
//	    dump_lir_inst(inst);
//	}
//    }
//}
//
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
//
//static void dump_trace(trace_recorder_t *rec)
//{
//    if (DUMP_LIR > 0) {
//	unsigned i;
//	trace_t *trace = rec->trace;
//	fprintf(stderr, "---------------\n");
//	fprintf(stderr, "trace %p %s\n", trace,
//#if JIT_DEBUG_TRACE
//	        trace->func_name
//#else
//	        ""
//#endif
//	        );
//
//	fprintf(stderr, "---------------\n");
//	for (i = 0; i < rec->bblist.size; i++) {
//	    basicblock_t *bb = (basicblock_t *)jit_list_get(&rec->bblist, i);
//	    fprintf(stderr, "---------------\n");
//	    dump_lir_block(bb);
//	}
//	fprintf(stderr, "---------------\n");
//	dump_side_exit(rec);
//	fprintf(stderr, "---------------\n");
//    }
//}
//
///* regstack { */
//static regstack_t *regstack_init(regstack_t *stack, VALUE *pc)
//{
//    int i;
//    jit_list_init(&stack->list);
//    for (i = 0; i < LIR_RESERVED_REGSTACK_SIZE; i++) {
//	jit_list_add(&stack->list, 0);
//    }
//    stack->flag = TRACE_EXIT_SIDE_EXIT;
//    stack->status = REGSTACK_DEFAULT;
//    stack->pc = pc;
//    stack->refc = 0;
//    return stack;
//}
//
//static regstack_t *regstack_new(struct memory_pool *mpool, VALUE *pc)
//{
//    regstack_t *stack = (regstack_t *)memory_pool_alloc(mpool, sizeof(*stack));
//    return regstack_init(stack, pc);
//}
//
//static void regstack_push(trace_recorder_t *rec, regstack_t *stack, lir_t reg)
//{
//    assert(reg != NULL);
//    jit_list_add(&stack->list, (uintptr_t)reg);
//    if (DUMP_STACK_MAP) {
//	fprintf(stderr, "push: %d %p\n", stack->list.size, reg);
//    }
//}
//
//static lir_t regstack_pop(trace_recorder_t *rec, regstack_t *stack, int *popped)
//{
//    lir_t reg;
//    if (stack->list.size == 0) {
//	assert(0 && "FIXME stack underflow");
//    }
//    reg = (lir_t)jit_list_get(&stack->list, stack->list.size - 1);
//    stack->list.size--;
//    if (reg == NULL) {
//	reg = trace_recorder_insert_stackpop(rec);
//	*popped = 1;
//    }
//    if (DUMP_STACK_MAP) {
//	fprintf(stderr, "pop: %d %p\n", stack->list.size, reg);
//    }
//    return reg;
//}
//
//static regstack_t *regstack_clone(struct memory_pool *mpool, regstack_t *old, VALUE *pc)
//{
//    unsigned i;
//    regstack_t *stack = regstack_new(mpool, pc);
//    jit_list_ensure(&stack->list, old->list.size);
//    stack->list.size = 0;
//    for (i = 0; i < old->list.size; i++) {
//	jit_list_add(&stack->list, jit_list_get(&old->list, i));
//    }
//    return stack;
//}
//
//static lir_t regstack_get_direct(regstack_t *stack, int n)
//{
//    return (lir_t)jit_list_get(&stack->list, n);
//}
//
//static void regstack_set_direct(regstack_t *stack, int n, lir_t val)
//{
//    jit_list_set(&stack->list, n, (uintptr_t)val);
//}
//
//static void regstack_set(regstack_t *stack, int n, lir_t reg)
//{
//    n = stack->list.size - n - 1;
//    if (DUMP_STACK_MAP) {
//	fprintf(stderr, "set: %d %p\n", n, reg);
//    }
//    assert(reg != NULL);
//    regstack_set_direct(stack, n, reg);
//}
//
//static lir_t regstack_top(regstack_t *stack, int n)
//{
//    lir_t reg;
//    int idx = stack->list.size - n - 1;
//    assert(0 <= idx && idx < (int)stack->list.size);
//    reg = (lir_t)jit_list_get(&stack->list, idx);
//    if (reg == NULL) {
//	assert(0 && "FIXME stack underflow");
//    }
//    assert(reg != NULL);
//    if (DUMP_STACK_MAP) {
//	fprintf(stderr, "top: %d %p\n", n, reg);
//    }
//    return reg;
//}
//
//static int regstack_equal(regstack_t *s1, regstack_t *s2)
//{
//    return jit_list_equal(&s1->list, &s2->list);
//}
//
//static void regstack_dump(regstack_t *stack)
//{
//    unsigned i;
//    fprintf(stderr, "stack=%p size=%d\n", stack, stack->list.size);
//    for (i = 0; i < stack->list.size; i++) {
//	lir_t reg = (lir_t)jit_list_get(&stack->list, i);
//	if (reg) {
//	    fprintf(stderr, "[%u] = %ld\n", i, lir_getid(reg));
//	}
//    }
//}
//
//static void regstack_delete(regstack_t *stack)
//{
//    jit_list_delete(&stack->list);
//    if (JIT_DEBUG_VERBOSE >= 10) {
//	memset(stack, 0, sizeof(*stack));
//    }
//}
///* } regstack */
//
