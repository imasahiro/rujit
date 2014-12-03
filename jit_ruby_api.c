// imported api from ruby-core
extern void vm_search_method(rb_call_info_t *ci, VALUE recv);
extern VALUE rb_f_block_given_p(void);
extern VALUE rb_reg_new_ary(VALUE ary, int opt);
VALUE check_match(VALUE pattern, VALUE target, enum vm_check_match_type type);

static inline int
check_cfunc(const rb_method_entry_t *me, VALUE (*func)())
{
    return me && me->def->type == VM_METHOD_TYPE_CFUNC && me->def->body.cfunc.func == func;
}

/* original code is copied from vm_insnhelper.c vm_getivar() */
static int vm_load_cache(VALUE obj, ID id, IC ic, rb_call_info_t *ci, int is_attr)
{
    if (RB_TYPE_P(obj, T_OBJECT)) {
	VALUE klass = RBASIC(obj)->klass;
	st_data_t index;
	long len = ROBJECT_NUMIV(obj);
	struct st_table *iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);

	if (iv_index_tbl) {
	    if (st_lookup(iv_index_tbl, id, &index)) {
		if ((long)index < len) {
		    //VALUE val = Qundef;
		    //VALUE *ptr = ROBJECT_IVPTR(obj);
		    //val = ptr[index];
		}
		if (!is_attr) {
		    ic->ic_value.index = index;
		    ic->ic_serial = RCLASS_SERIAL(klass);
		}
		else { /* call_info */
		    ci->aux.index = (int)index + 1;
		}
		return 1;
	    }
	}
    }
    return 0;
}

static int vm_load_or_insert_ivar(VALUE obj, ID id, VALUE val, IC ic, CALL_INFO ci, int is_attr)
{
    if (vm_load_cache(obj, id, ic, ci, is_attr)) {
	return 1;
    }
    rb_ivar_set(obj, id, val);
    if (vm_load_cache(obj, id, ic, ci, is_attr)) {
	return 2;
    }
    return 0;
}

static VALUE
make_no_method_exception(VALUE exc, const char *format, VALUE obj, int argc, const VALUE *argv)
{
    int n = 0;
    VALUE mesg;
    VALUE args[3];

    if (!format) {
	format = "undefined method `%s' for %s";
    }
    mesg = rb_const_get(exc, rb_intern("message"));
    if (rb_method_basic_definition_p(CLASS_OF(mesg), '!')) {
	args[n++] = rb_name_err_mesg_new(mesg, rb_str_new2(format), obj, argv[0]);
    }
    else {
	args[n++] = rb_funcall(mesg, '!', 3, rb_str_new2(format), obj, argv[0]);
    }
    args[n++] = argv[0];
    if (exc == rb_eNoMethodError) {
	args[n++] = rb_ary_new4(argc - 1, argv + 1);
    }
    return rb_class_new_instance(n, args, exc);
}

/* redefined_flag { */
static st_table *jit_opt_method_table = 0;
static short jit_vm_redefined_flag[JIT_BOP_EXT_LAST_];

#define MATH_REDEFINED_OP_FLAG (1 << 9)
static int
jit_redefinition_check_flag(VALUE klass)
{
    if (klass == rb_cFixnum)
	return FIXNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cFloat)
	return FLOAT_REDEFINED_OP_FLAG;
    if (klass == rb_cString)
	return STRING_REDEFINED_OP_FLAG;
    if (klass == rb_cArray)
	return ARRAY_REDEFINED_OP_FLAG;
    if (klass == rb_cHash)
	return HASH_REDEFINED_OP_FLAG;
    if (klass == rb_cBignum)
	return BIGNUM_REDEFINED_OP_FLAG;
    if (klass == rb_cSymbol)
	return SYMBOL_REDEFINED_OP_FLAG;
    if (klass == rb_cTime)
	return TIME_REDEFINED_OP_FLAG;
    if (klass == rb_cRegexp)
	return REGEXP_REDEFINED_OP_FLAG;
    if (klass == rb_cMath)
	return MATH_REDEFINED_OP_FLAG;
    return 0;
}

void rujit_check_redefinition_opt_method(const rb_method_entry_t *me, VALUE klass)
{
#define ruby_vm_redefined_flag GET_VM()->redefined_flag
    st_data_t bop;
    int flag;
    if (UNLIKELY(disable_jit)) {
	return;
    }
    flag = jit_redefinition_check_flag(klass);
    if (st_lookup(jit_opt_method_table, (st_data_t)me, &bop)) {
	assert(flag != 0);
	if ((jit_vm_redefined_flag[bop] & ruby_vm_redefined_flag[bop]) == ruby_vm_redefined_flag[bop]) {
	    jit_vm_redefined_flag[bop] |= ruby_vm_redefined_flag[bop];
	}
	jit_vm_redefined_flag[bop] |= flag;
    }
}

/* copied from vm.c */
static void add_opt_method(VALUE klass, ID mid, VALUE bop)
{
    rb_method_entry_t *me = rb_method_entry_at(klass, mid);

    if (me && me->def && me->def->type == VM_METHOD_TYPE_CFUNC) {
	st_insert(jit_opt_method_table, (st_data_t)me, (st_data_t)bop);
    }
    else {
	rb_bug("undefined optimized method: %s", rb_id2name(mid));
    }
}

static void rujit_init_redefined_flag(void)
{
#define DEF(k, mid, bop)                              \
    do {                                              \
	jit_vm_redefined_flag[bop] = 0;               \
	add_opt_method(rb_c##k, rb_intern(mid), bop); \
    } while (0)
    jit_opt_method_table = st_init_numtable();
    DEF(Fixnum, "&", JIT_BOP_AND);
    DEF(Fixnum, "|", JIT_BOP_OR);
    DEF(Fixnum, "^", JIT_BOP_XOR);
    DEF(Fixnum, ">>", JIT_BOP_RSHIFT);
    DEF(Fixnum, "~", JIT_BOP_INV);
    DEF(Fixnum, "**", JIT_BOP_POW);
    DEF(Float, "**", JIT_BOP_POW);

    DEF(Fixnum, "-@", JIT_BOP_NEG);
    DEF(Float, "-@", JIT_BOP_NEG);
    DEF(Fixnum, "to_f", JIT_BOP_TO_F);
    DEF(Float, "to_f", JIT_BOP_TO_F);
    DEF(String, "to_f", JIT_BOP_TO_F);

    DEF(Float, "to_i", JIT_BOP_TO_I);
    DEF(String, "to_i", JIT_BOP_TO_I);

    DEF(Fixnum, "to_s", JIT_BOP_TO_S);
    DEF(Float, "to_s", JIT_BOP_TO_S);
    DEF(String, "to_s", JIT_BOP_TO_S);

    DEF(Math, "sin", JIT_BOP_SIN);
    DEF(Math, "cos", JIT_BOP_COS);
    DEF(Math, "tan", JIT_BOP_TAN);
    DEF(Math, "exp", JIT_BOP_EXP);
    DEF(Math, "sqrt", JIT_BOP_SQRT);
    DEF(Math, "log10", JIT_BOP_LOG10);
    DEF(Math, "log2", JIT_BOP_LOG2);
#undef DEF
}
/* } redefined_flag */
