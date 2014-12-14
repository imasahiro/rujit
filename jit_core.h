/**********************************************************************

  jit_core.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

/* lir_inst {*/
typedef struct lir_basicblock_t basicblock_t;

typedef struct lir_inst_t {
    unsigned id;
    unsigned short opcode;
    unsigned short flag;
    basicblock_t *parent;
    jit_list_t *user;
} lir_inst_t, *lir_t;

static int basicblock_id(basicblock_t *bb);

static lir_t lir_inst_init(lir_t inst, size_t size, unsigned opcode)
{
    memset(inst, 0, size);
    inst->opcode = opcode;
    return inst;
}

static int lir_getid(lir_t inst)
{
    return inst->id;
}

static void lir_delete(lir_t inst)
{
    if (inst->user) {
	jit_list_delete(inst->user);
	inst->user = NULL;
    }
}

#define LIR_NEWINST(T) ((T *)lir_inst_init((lir_t)alloca(sizeof(T)), sizeof(T), OPCODE_##T))
#define LIR_NEWINST_N(T, SIZE) \
    ((T *)lir_inst_init((lir_t)alloca(sizeof(T) + sizeof(lir_t) * (SIZE)), sizeof(T) + sizeof(lir_t) * (SIZE), OPCODE_##T))

/* } lir_inst */

#define ADD_INST(BUILDER, INST) ADD_INST_N(BUILDER, INST, 0)

#define ADD_INST_N(BUILDER, INST, SIZE) \
    lir_builder_add_inst(BUILDER, &(INST)->base, sizeof(*INST) + sizeof(lir_t) * (SIZE))

static lir_t lir_builder_add_inst(lir_builder_t *self, lir_t inst, size_t size);
#include "lir_template.h"
#include "lir.c"

static int lir_is_terminator(lir_t inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_TERMINATOR_##OPNAME;
	// 	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static int lir_is_guard(lir_t inst)
{
    switch (inst->opcode) {
#define IS_TERMINATOR(OPNAME) \
    case OPCODE_I##OPNAME:    \
	return LIR_IS_GUARD_INST_##OPNAME;
	// 	LIR_EACH(IS_TERMINATOR);
	default:
	    assert(0 && "unreachable");
#undef IS_TERMINATOR
    }
    return 0;
}

static lir_t *lir_inst_get_args(lir_t inst, int idx)
{
#define GET_ARG(OPNAME)    \
    case OPCODE_I##OPNAME: \
	return GetNext_##OPNAME(inst, idx);

    switch (inst->opcode) {
	// 	LIR_EACH(GET_ARG);
	default:
	    assert(0 && "unreachable");
    }
#undef GET_ARG
    TODO("");
    return NULL;
}

static enum lir_type lir_get_type(lir_t inst)
{
    switch (inst->opcode) {
#define LIR_GET_TYPE(OPNAME) \
    case OPCODE_I##OPNAME:   \
	return LIR_TYPE_##OPNAME;
	LIR_EACH(LIR_GET_TYPE);
	default:
	    assert(0 && "unreachable");
#undef LIR_GET_TYPE
    }
    return LIR_TYPE_ERROR;
}

static void lir_inst_adduser(memory_pool_t *mpool, lir_t inst, lir_t ir)
{
    if (inst->user == NULL) {
	inst->user = (jit_list_t *)memory_pool_alloc(mpool, sizeof(jit_list_t));
	jit_list_init(inst->user);
    }
    jit_list_add(inst->user, (uintptr_t)ir);
}

static void lir_inst_removeuser(lir_t inst, lir_t ir)
{
    if (inst->user == NULL) {
	return;
    }
    jit_list_remove(inst->user, (uintptr_t)ir);
    if (inst->user->size == 0) {
	jit_list_delete(inst->user);
	inst->user = NULL;
    }
}

static void lir_update_userinfo(memory_pool_t *mpool, lir_t inst)
{
    lir_t *ref = NULL;
    int i = 0;
    while ((ref = lir_inst_get_args(inst, i)) != NULL) {
	lir_t user = *ref;
	if (user) {
	    lir_inst_adduser(mpool, user, inst);
	}
	i += 1;
    }
}

/* basicblock { */
struct lir_basicblock_t {
    lir_inst_t base;
    VALUE *pc;
    struct local_var_table_t *init_table;
    struct local_var_table_t *last_table;
    jit_list_t insts;
    jit_list_t preds;
    jit_list_t succs;
    jit_list_t side_exits; // n -> PC, n+1 -> reg2stack map
};

static basicblock_t *basicblock_new(memory_pool_t *mpool, VALUE *pc, int id)
{
    basicblock_t *bb = (basicblock_t *)memory_pool_alloc(mpool, sizeof(*bb));
    bb->pc = pc;
    bb->init_table = NULL;
    bb->last_table = NULL;
    jit_list_init(&bb->insts);
    jit_list_init(&bb->preds);
    jit_list_init(&bb->succs);
    jit_list_init(&bb->side_exits);
    bb->base.id = id;
    return bb;
}
static int basicblock_id(basicblock_t *bb)
{
    return bb->base.id;
}

static unsigned basicblock_size(basicblock_t *bb)
{
    return bb->insts.size;
}

static void basicblock_delete(basicblock_t *bb)
{
    unsigned i;
    for (i = 0; i < basicblock_size(bb); i++) {
	lir_t inst = JIT_LIST_GET(lir_t, &bb->insts, i);
	lir_delete(inst);
    }
    jit_list_delete(&bb->insts);
    jit_list_delete(&bb->preds);
    jit_list_delete(&bb->succs);
    jit_list_delete(&bb->side_exits);

    if (bb->init_table) {
	TODO("");
	// local_var_table_delete(bb->init_table);
    }
    if (bb->last_table) {
	TODO("");
	// local_var_table_delete(bb->last_table);
    }
}

static void basicblock_link(basicblock_t *bb, basicblock_t *child)
{
    JIT_LIST_ADD(&bb->succs, child);
    JIT_LIST_ADD(&child->preds, bb);
}

static basicblock_t *basicblock_get_succ(basicblock_t *bb, unsigned idx)
{
    return JIT_LIST_GET(basicblock_t *, &bb->succs, idx);
}

static basicblock_t *basicblock_get_pred(basicblock_t *bb, unsigned idx)
{
    return JIT_LIST_GET(basicblock_t *, &bb->preds, idx);
}

static void basicblock_unlink(basicblock_t *bb, basicblock_t *child)
{
    JIT_LIST_REMOVE(&bb->succs, child);
    JIT_LIST_REMOVE(&child->preds, bb);
}

static void basicblock_insert_inst_before(basicblock_t *bb, lir_t target, lir_t val)
{
    int idx1 = JIT_LIST_INDEXOF(&bb->insts, target);
    int idx2 = JIT_LIST_INDEXOF(&bb->insts, val);
    assert(idx1 >= 0 && idx2 >= 0);
    JIT_LIST_REMOVE(&bb->insts, val);
    JIT_LIST_INSERT(&bb->insts, idx1, val);
}

static void basicblock_insert_inst_after(basicblock_t *bb, lir_t target, lir_t val)
{
    int idx1 = JIT_LIST_INDEXOF(&bb->insts, target);
    int idx2 = JIT_LIST_INDEXOF(&bb->insts, val);
    assert(idx1 >= 0 && idx2 >= 0);
    JIT_LIST_REMOVE(&bb->insts, val);
    JIT_LIST_INSERT(&bb->insts, idx1 + 1, val);
}

static void basicblock_swap_inst(basicblock_t *bb, int idx1, int idx2)
{
    lir_t inst1 = JIT_LIST_GET(lir_t, &bb->insts, idx1);
    lir_t inst2 = JIT_LIST_GET(lir_t, &bb->insts, idx2);
    JIT_LIST_SET(&bb->insts, idx1, inst2);
    JIT_LIST_SET(&bb->insts, idx2, inst1);
}

static void basicblock_append(basicblock_t *bb, lir_inst_t *inst)
{
    JIT_LIST_ADD(&bb->insts, inst);
    inst->parent = bb;
}

static lir_t basicblock_get(basicblock_t *bb, int i)
{
    return JIT_LIST_GET(lir_t, &bb->insts, i);
}

static int basicblock_get_index(basicblock_t *bb, lir_t inst)
{
    return JIT_LIST_INDEXOF(&bb->insts, inst);
}

static lir_t basicblock_get_last(basicblock_t *bb)
{
    if (basicblock_size(bb) == 0) {
	return NULL;
    }
    return basicblock_get(bb, basicblock_size(bb) - 1);
}

static lir_t basicblock_get_terminator(basicblock_t *bb)
{
    lir_t val = basicblock_get_last(bb);
    if (lir_is_terminator(val)) {
	return val;
    }
    return NULL;
}

static lir_t basicblock_get_next(basicblock_t *bb, lir_t inst)
{
    int idx = basicblock_get_index(bb, inst);
    if (0 <= idx && idx - 1 < (int)basicblock_size(bb)) {
	return basicblock_get(bb, idx + 1);
    }
    return NULL;
}

/* } basicblock */

/* lir_func { */
typedef struct lir_func_t {
    unsigned id;
    VALUE *pc;
    basicblock_t *entry_bb;
    struct native_func_t *compiled_code;
    jit_list_t constants;
    jit_list_t method_cache;
    jit_list_t bblist;
    jit_list_t side_exits; /* Array<side_exit_handler_t*> */
} lir_func_t;

static lir_func_t *lir_func_new(memory_pool_t *mp)
{
    lir_func_t *func = MEMORY_POOL_ALLOC(lir_func_t, mp);
    jit_list_init(&func->bblist);
    jit_list_init(&func->side_exits);
    func->id = current_jit->func_id++;
    return func;
}

static void lir_func_delete(lir_func_t *func)
{
    jit_list_delete(&func->constants);
    jit_list_delete(&func->method_cache);
    jit_list_delete(&func->bblist);
    jit_list_delete(&func->side_exits);
}
/* } lir_func */

/* lir_builder_t {*/

static lir_builder_t *lir_builder_init(lir_builder_t *self, memory_pool_t *mpool)
{
    self->mode = LIR_BUILDER_STATE_NOP;
    self->cur_func = NULL;
    self->cur_bb = NULL;
    self->mpool = mpool;
    self->inst_size = 0;
    jit_list_init(&self->shadow_stack);
    return self;
}

static basicblock_t *lir_builder_create_block(lir_builder_t *builder, VALUE *pc)
{
    unsigned id = jit_list_size(&builder->cur_func->bblist);
    basicblock_t *bb = basicblock_new(builder->mpool, pc, id);
    JIT_LIST_ADD(&builder->cur_func->bblist, bb);
    return bb;
}

static void dump_lir_func(lir_func_t *func);
static void lir_builder_compile(rujit_t *jit, lir_builder_t *self)
{
    self->mode = LIR_BUILDER_STATE_COMPILING;
    dump_lir_func(self->cur_func);
    TODO("");
    self->mode = LIR_BUILDER_STATE_NOP;
}

static int lir_builder_is_full(lir_builder_t *self)
{
    return self->inst_size >= LIR_MAX_TRACE_LENGTH;
}

static void lir_builder_abort(lir_builder_t *self)
{
    self->mode = LIR_BUILDER_STATE_NOP;
    TODO("");
}

static void lir_builder_set_bb(lir_builder_t *self, basicblock_t *bb)
{
    self->cur_bb = bb;
}

static basicblock_t *lir_builder_cur_bb(lir_builder_t *self)
{
    return self->cur_bb;
}

static basicblock_t *lir_builder_entry_bb(lir_builder_t *self)
{
    return JIT_LIST_GET(basicblock_t *, &self->cur_func->bblist, 0);
}

static basicblock_t *lir_builder_find_block(lir_builder_t *self, VALUE *pc)
{
    int i;
    jit_list_t *bblist = &self->cur_func->bblist;
    for (i = (int)jit_list_size(bblist) - 1; i >= 0; i--) {
	basicblock_t *bb = JIT_LIST_GET(basicblock_t *, bblist, i);
	if (bb->pc == pc) {
	    return bb;
	}
    }
    return NULL;
}

static lir_t Emit_Jump(lir_builder_t *builder, basicblock_t *bb);

static void lir_builder_reset(lir_builder_t *self, lir_func_t **func, VALUE *pc)
{
    basicblock_t *entry_bb, *cur_bb;
    assert(self->mode == LIR_BUILDER_STATE_NOP);
    assert(*func == NULL);
    *func = lir_func_new(self->mpool);
    self->cur_func = *func;
    self->shadow_stack.size = 0;
    hashmap_dispose(&self->const_pool, NULL);
    hashmap_init(&self->const_pool, 4);

    entry_bb = lir_builder_create_block(self, pc);
    cur_bb = lir_builder_create_block(self, pc);
    lir_builder_set_bb(self, entry_bb);
    Emit_Jump(self, cur_bb);
    lir_builder_set_bb(self, cur_bb);
}

static lir_t lir_builder_pop(lir_builder_t *self)
{
    unsigned size = jit_list_size(&self->shadow_stack);
    lir_t val;
    assert(size > 0);
    return (lir_t)jit_list_remove_idx(&self->shadow_stack, size - 1);
}

static lir_t lir_builder_add_inst(lir_builder_t *self, lir_t inst, size_t size)
{
    lir_t newinst = NULL;
    if (LIR_OPT_PEEPHOLE_OPTIMIZATION) {
	// TODO
    }
    newinst = (lir_t)memory_pool_alloc(self->mpool, size);
    memcpy(newinst, inst, size);
    newinst->id = self->inst_size++;
    basicblock_append(lir_builder_cur_bb(self), newinst);
    return newinst;
}

static void lir_builder_push(lir_builder_t *self, lir_t val)
{
    JIT_LIST_ADD(&self->shadow_stack, val);
}

static lir_t lir_builder_get_const(lir_builder_t *builder, VALUE val)
{
    return (lir_t)hashmap_get(&builder->const_pool, (hashmap_data_t)val);
}

static lir_t lir_builder_add_const(lir_builder_t *builder, VALUE val, lir_t Rval)
{
    lir_t Rold = lir_builder_get_const(builder, val);
    if (Rold) {
	return Rold;
    }
    hashmap_set(&builder->const_pool, (hashmap_data_t)val, (hashmap_data_t)Rval);
    return Rval;
}

static void lir_builder_dispose(lir_builder_t *self)
{
    assert(self->mode == LIR_BUILDER_STATE_NOP);
    jit_list_delete(&self->shadow_stack);
    hashmap_dispose(&self->const_pool, NULL);
    TODO("");
}

/* lir_builder_t }*/

/* native_func_t {*/

typedef struct rujit_backend_t {
    void *ctx;
    void (*f_init)(rujit_t *, struct rujit_backend_t *self);
    void (*f_delete)(rujit_t *, struct rujit_backend_t *self);
    native_raw_func_t *(*f_compile)(rujit_t *, void *ctx, lir_func_t *func);
    void (*f_unload)(rujit_t *, void *ctx, native_func_t *func);
} rujit_backend_t;

static void dummy_init(rujit_t *jit, struct rujit_backend_t *self)
{
}

static void dummy_delete(rujit_t *jit, struct rujit_backend_t *self)
{
}

static native_raw_func_t *dummy_compile(rujit_t *jit, void *ctx, lir_func_t *func)
{
    return NULL;
}

static void dummy_unload(rujit_t *jit, void *ctx, native_func_t *func)
{
}

static rujit_backend_t backend_dummy = {
    NULL,
    dummy_init,
    dummy_delete,
    dummy_compile,
    dummy_unload
};

#define RC_INC(O) ((O)->refc++)
#define RC_DEC(O) (--(O)->refc)
#define RC_INIT(O) ((O)->refc = 1)
#define RC_CHECK(O) ((O)->refc == 0)

// static native_func_t *native_func_new(lir_func_t *origin)
// {
//     native_func_t *func = (native_func_t *)malloc(sizeof(native_func_t));
//     func->flag = 0;
//     RC_INIT(func);
//     func->handler = NULL;
//     func->code = NULL;
//     func->origin = origin;
//     return func;
// }

static void native_func_invalidate(native_func_t *func)
{
    rujit_t *jit = current_jit;
    jit->backend->f_unload(jit, jit->backend->ctx, func);
}

static void native_func_delete(native_func_t *func)
{
    RC_DEC(func);
    assert(RC_CHECK(func));
    free(func);
}

// enum native_func_call_status {
//     NATIVE_FUNC_ERROR = 0,
//     NATIVE_FUNC_SUCCESS = 1,
//     NATIVE_FUNC_DELETED = 1 << 1,
//     NATIVE_FUNC_INVALIDATED = 1 << 2
// };
//
// static int native_func_invoke(native_func_t *func, VALUE *return_value)
// {
//     *return_value = 0;
//     RC_INC(func);
//     *return_value = ((native_raw_func_t)func->code)();
//     RC_DEC(func);
//     if ((func->flag & NATIVE_FUNC_INVALIDATED) && RC_CHECK0(func)) {
// 	native_func_delete(func);
// 	return NATIVE_FUNC_DELETED;
//     }
//     return NATIVE_FUNC_SUCCESS;
// }
//
// static void native_func_manager_remove(native_func_manager_t *mng, VALUE key)
// {
//     hashmap_delete(&mng->codes, (hashmap_entry_destructor_t) native_func_delete);
// }
//
// static void native_func_manager_remove_all(native_func_manager_t *mng)
// {
//     hashmap_delete(&mng->codes, (hashmap_entry_destructor_t) native_func_delete);
// }
//
// static void native_func_manager_delete(native_func_manager_t *mng)
// {
//     native_func_manager_remove_all(mng);
// }

/* native_func_t }*/

/* native_func_manager_t {*/

static native_func_manager_t *native_func_manager_init(native_func_manager_t *self)
{
    hashmap_init(&self->traces, 4);
    jit_list_init(&self->compiled_codes);
    hashmap_init(&self->valid_code0, 1);
    hashmap_init(&self->valid_code1, 1);
    return self;
}

static void native_func_manager_add(native_func_manager_t *self, native_func_t *func)
{
    JIT_LIST_ADD(&self->compiled_codes, func);
}

static void native_func_manager_add_trace(native_func_manager_t *self, jit_trace_t *trace)
{
    hashmap_set(&self->traces, (hashmap_data_t)trace->start_pc, (hashmap_data_t)trace);
}

static jit_trace_t *native_func_manager_find_trace(native_func_manager_t *self, VALUE *pc)
{
    return (jit_trace_t *)hashmap_get(&self->traces, (uintptr_t)pc);
}

enum native_func_invalidate_type {
    NATIVE_FUNC_INVALIDATE_TYPE_METHOD = 0,
    NATIVE_FUNC_INVALIDATE_TYPE_BLOC = 1
};

static void native_func_manager_invalidate(native_func_manager_t *self, enum native_func_invalidate_type type, VALUE val)
{
    native_func_t *func = NULL;
    hashmap_t *valid_code = NULL;
    if (type == NATIVE_FUNC_INVALIDATE_TYPE_METHOD) {
	valid_code = &self->valid_code0;
    }
    else {
	valid_code = &self->valid_code1;
    }
    func = (native_func_t *)hashmap_get(valid_code, (uintptr_t)val);
    native_func_invalidate(func);
}

static void native_func_invalidate(native_func_t *func);
static void native_func_delete(native_func_t *func);

static void native_func_manager_dispose(native_func_manager_t *self)
{
    unsigned i;
    hashmap_dispose(&self->traces, (hashmap_entry_destructor_t)native_func_delete);
    for (i = 0; i < jit_list_size(&self->compiled_codes); i++) {
	native_func_t *func = JIT_LIST_GET(native_func_t *, &self->compiled_codes, i);
	native_func_delete(func);
    }
    jit_list_delete(&self->compiled_codes);
}

/* native_func_manager_t }*/

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
