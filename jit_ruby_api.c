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
