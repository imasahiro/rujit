/**********************************************************************

  jit_args.c - process method call arguments.

  $Author$

  Copyright (C) 2014- Yukihiro Matsumoto

**********************************************************************/

NORETURN(static void raise_argument_error(rb_thread_t *th, const rb_iseq_t *iseq, const VALUE exc));
NORETURN(static void argument_arity_error(rb_thread_t *th, const rb_iseq_t *iseq, const int miss_argc, const int min_argc, const int max_argc));
NORETURN(static void argument_kw_error(rb_thread_t *th, const rb_iseq_t *iseq, const char *error, const VALUE keys));
VALUE rb_keyword_error_new(const char *error, VALUE keys); /* class.c */

struct args_info {
    /* basic args info */
    rb_call_info_t *ci;
    VALUE *argv;
    lir_t *kw_entry;
    int argc;

    /* additional args info */
    int rest_index;
    VALUE *kw_argv;
    VALUE rest;
};

enum arg_setup_type {
    arg_setup_method,
    arg_setup_block,
    arg_setup_lambda
};

static inline int
args_argc(struct args_info *args)
{
    if (args->rest == Qfalse) {
	return args->argc;
    } else {
	return args->argc + RARRAY_LENINT(args->rest) - args->rest_index;
    }
}

static inline void
args_extend(struct args_info *args, const int min_argc)
{
    int i;

    if (args->rest) {
	args->rest = rb_ary_dup(args->rest);
	assert(args->rest_index == 0);
	for (i = args->argc + RARRAY_LENINT(args->rest); i < min_argc; i++) {
	    rb_ary_push(args->rest, Qnil);
	}
    } else {
	for (i = args->argc; i < min_argc; i++) {
	    args->argv[args->argc++] = Qnil;
	}
    }
}

static inline void
args_reduce(struct args_info *args, int over_argc)
{
    if (args->rest) {
	const long len = RARRAY_LEN(args->rest);

	if (len > over_argc) {
	    args->rest = rb_ary_dup(args->rest);
	    rb_ary_resize(args->rest, len - over_argc);
	    return;
	} else {
	    args->rest = Qfalse;
	    over_argc -= len;
	}
    }

    assert(args->argc >= over_argc);
    args->argc -= over_argc;
}

static inline int
args_check_block_arg0(struct args_info *args, rb_thread_t *th, const int msl)
{
    VALUE ary = Qnil;

    if (args->rest && RARRAY_LEN(args->rest) == 1) {
	VALUE arg0 = RARRAY_AREF(args->rest, 0);
	ary = rb_check_array_type(arg0);
	th->mark_stack_len = msl;
    } else if (args->argc == 1) {
	VALUE arg0 = args->argv[0];
	ary = rb_check_array_type(arg0);
	th->mark_stack_len = msl;
	args->argv[0] = arg0; /* see: https://bugs.ruby-lang.org/issues/8484 */
    }

    if (!NIL_P(ary)) {
	args->rest = ary;
	args->rest_index = 0;
	args->argc = 0;
	return TRUE;
    }

    return FALSE;
}

static inline void
args_copy(struct args_info *args)
{
    if (args->rest != Qfalse) {
	int argc = args->argc;
	args->argc = 0;
	args->rest = rb_ary_dup(args->rest); /* make dup */

	/*
	 * argv: [m0, m1, m2, m3]
	 * rest: [a0, a1, a2, a3, a4, a5]
	 *                ^
	 *                rest_index
	 *
	 * #=> first loop
	 *
	 * argv: [m0, m1]
	 * rest: [m2, m3, a2, a3, a4, a5]
	 *        ^
	 *        rest_index
	 *
	 * #=> 2nd loop
	 *
	 * argv: [] (argc == 0)
	 * rest: [m0, m1, m2, m3, a2, a3, a4, a5]
	 *        ^
	 *        rest_index
	 */
	while (args->rest_index > 0 && argc > 0) {
	    RARRAY_ASET(args->rest, --args->rest_index, args->argv[--argc]);
	}
	while (argc > 0) {
	    rb_ary_unshift(args->rest, args->argv[--argc]);
	}
    } else if (args->argc > 0) {
	args->rest = rb_ary_new_from_values(args->argc, args->argv);
	args->rest_index = 0;
	args->argc = 0;
    }
}

static inline const VALUE *
args_rest_argv(struct args_info *args)
{
    return RARRAY_CONST_PTR(args->rest) + args->rest_index;
}

static inline VALUE
args_rest_array(struct args_info *args)
{
    VALUE ary;

    if (args->rest) {
	ary = rb_ary_subseq(args->rest, args->rest_index, RARRAY_LEN(args->rest) - args->rest_index);
	args->rest = 0;
    } else {
	ary = rb_ary_new();
    }
    return ary;
}

static int
keyword_hash_p(VALUE *kw_hash_ptr, VALUE *rest_hash_ptr, rb_thread_t *th, const int msl)
{
    *rest_hash_ptr = rb_check_hash_type(*kw_hash_ptr);
    th->mark_stack_len = msl;

    if (!NIL_P(*rest_hash_ptr)) {
	VALUE hash = rb_extract_keywords(rest_hash_ptr);
	if (!hash)
	    hash = Qnil;
	*kw_hash_ptr = hash;
	return TRUE;
    } else {
	*kw_hash_ptr = Qnil;
	return FALSE;
    }
}

static VALUE
args_pop_keyword_hash(struct args_info *args, VALUE *kw_hash_ptr, rb_thread_t *th, const int msl)
{
    VALUE rest_hash;

    if (args->rest == Qfalse) {
    from_argv:
	assert(args->argc > 0);
	*kw_hash_ptr = args->argv[args->argc - 1];

	if (keyword_hash_p(kw_hash_ptr, &rest_hash, th, msl)) {
	    if (rest_hash) {
		args->argv[args->argc - 1] = rest_hash;
	    } else {
		args->argc--;
		return TRUE;
	    }
	}
    } else {
	long len = RARRAY_LEN(args->rest);

	if (len > 0) {
	    *kw_hash_ptr = RARRAY_AREF(args->rest, len - 1);

	    if (keyword_hash_p(kw_hash_ptr, &rest_hash, th, msl)) {
		if (rest_hash) {
		    RARRAY_ASET(args->rest, len - 1, rest_hash);
		} else {
		    args->rest = rb_ary_dup(args->rest);
		    rb_ary_pop(args->rest);
		    return TRUE;
		}
	    }
	} else {
	    goto from_argv;
	}
    }

    return FALSE;
}

static int
args_kw_argv_to_hash(struct args_info *args)
{
    const VALUE *const passed_keywords = args->ci->kw_arg->keywords;
    const int kw_len = args->ci->kw_arg->keyword_len;
    VALUE h = rb_hash_new();
    const int kw_start = args->argc - kw_len;
    const VALUE *const kw_argv = args->argv + kw_start;
    int i;

    args->argc = kw_start + 1;
    for (i = 0; i < kw_len; i++) {
	rb_hash_aset(h, passed_keywords[i], kw_argv[i]);
    }

    args->argv[args->argc - 1] = h;

    return args->argc;
}

static void
args_stored_kw_argv_to_hash(struct args_info *args)
{
    VALUE h = rb_hash_new();
    int i;
    const VALUE *const passed_keywords = args->ci->kw_arg->keywords;
    const int passed_keyword_len = args->ci->kw_arg->keyword_len;

    for (i = 0; i < passed_keyword_len; i++) {
	rb_hash_aset(h, passed_keywords[i], args->kw_argv[i]);
    }
    args->kw_argv = NULL;

    if (args->rest) {
	args->rest = rb_ary_dup(args->rest);
	rb_ary_push(args->rest, h);
    } else {
	args->argv[args->argc++] = h;
    }
}

static inline void
args_setup_lead_parameters(struct args_info *args, int argc, VALUE *locals)
{
    if (args->argc >= argc) {
	/* do noting */
	args->argc -= argc;
	args->argv += argc;
    } else {
	int i, j;
	const VALUE *argv = args_rest_argv(args);

	for (i = args->argc, j = 0; i < argc; i++, j++) {
	    locals[i] = argv[j];
	}
	args->rest_index += argc - args->argc;
	args->argc = 0;
    }
}

static inline void
args_setup_post_parameters(struct args_info *args, int argc, VALUE *locals)
{
    long len;
    args_copy(args);
    len = RARRAY_LEN(args->rest);
    MEMCPY(locals, RARRAY_CONST_PTR(args->rest) + len - argc, VALUE, argc);
    rb_ary_resize(args->rest, len - argc);
}

static inline int
args_setup_opt_parameters(struct args_info *args, int opt_max, VALUE *locals)
{
    int i;

    if (args->argc >= opt_max) {
	args->argc -= opt_max;
	args->argv += opt_max;
	i = opt_max;
    } else {
	int j;
	i = args->argc;
	args->argc = 0;

	if (args->rest) {
	    int len = RARRAY_LENINT(args->rest);
	    const VALUE *argv = RARRAY_CONST_PTR(args->rest);

	    for (; i < opt_max && args->rest_index < len; i++, args->rest_index++) {
		locals[i] = argv[args->rest_index];
	    }
	}

	/* initialize by nil */
	for (j = i; j < opt_max; j++) {
	    locals[j] = Qnil;
	}
    }

    return i;
}

static inline void
args_setup_rest_parameter(struct args_info *args, VALUE *locals)
{
    args_copy(args);
    *locals = args_rest_array(args);
}

static VALUE
make_unused_kw_hash(const VALUE *passed_keywords, int passed_keyword_len, const VALUE *kw_argv, const int key_only)
{
    int i;
    VALUE obj = key_only ? rb_ary_tmp_new(1) : rb_hash_new();

    for (i = 0; i < passed_keyword_len; i++) {
	if (kw_argv[i] != Qundef) {
	    if (key_only) {
		rb_ary_push(obj, passed_keywords[i]);
	    } else {
		rb_hash_aset(obj, passed_keywords[i], kw_argv[i]);
	    }
	}
    }
    return obj;
}

static inline int
args_setup_kw_parameters_lookup(const ID key, const VALUE *const passed_keywords, lir_t *passed_entry, int passed_keyword_len)
{
    int i;
    const VALUE keyname = ID2SYM(key);

    for (i = 0; i < passed_keyword_len; i++) {
	if (keyname == passed_keywords[i]) {
	    // *ptr = passed_values[i];
	    // passed_values[i] = Qundef;
	    passed_entry[i] = NULL;
	    return TRUE;
	}
    }

    return FALSE;
}

struct lir_argument {
    lir_t *passed_entry;
    jit_snapshot_t *snapshot;
};

static int args_setup_kw_parameters(lir_builder_t *builder, struct lir_argument *arg, int passed_keyword_len, const VALUE *const passed_keywords, const rb_iseq_t *const iseq)
{
    const ID *acceptable_keywords = iseq->param.keyword->table;
    const int req_key_num = iseq->param.keyword->required_num;
    const int key_num = iseq->param.keyword->num;
    const VALUE *const default_values = iseq->param.keyword->default_values;
    int i, di, found = 0;
    int unspecified_bits = 0;
    VALUE unspecified_bits_value = Qnil;

    for (i = 0; i < req_key_num; i++) {
	ID key = acceptable_keywords[i];
	if (args_setup_kw_parameters_lookup(key, passed_keywords, arg->passed_entry, passed_keyword_len)) {
	    found++;
	} else {
	    lir_builder_abort(builder, arg->snapshot, TRACE_ERROR_ARGUMENT, "missing keyword argument");
	    return 0;
	}
    }

    for (di = 0; i < key_num; i++, di++) {
	if (args_setup_kw_parameters_lookup(acceptable_keywords[i], passed_keywords, arg->passed_entry, passed_keyword_len)) {
	    found++;
	} else {
	    if (default_values[di] == Qundef) {
		// locals[i] = Qnil;

		if (LIKELY(i < 32)) { /* TODO: 32 -> Fixnum's max bits */
		    unspecified_bits |= 0x01 << di;
		} else {
		    if (NIL_P(unspecified_bits_value)) {
			/* fixnum -> hash */
			int j;
			unspecified_bits_value = rb_hash_new();

			for (j = 0; j < 32; j++) {
			    if (unspecified_bits & (0x01 << j)) {
				rb_hash_aset(unspecified_bits_value, INT2FIX(j), Qtrue);
			    }
			}
		    }
		    rb_hash_aset(unspecified_bits_value, INT2FIX(di), Qtrue);
		}
	    } else {
		// locals[i] = default_values[di];
	    }
	}
    }

    if (iseq->param.flags.has_kwrest) {
	const int rest_hash_index = key_num + 1;
	// locals[rest_hash_index] = make_unused_kw_hash(passed_keywords, passed_keyword_len, FALSE);
    } else {
	if (found != passed_keyword_len) {
	    TODO("argument_kw_error");
	    // VALUE keys = make_unused_kw_hash(passed_keywords, passed_keyword_len, TRUE);
	    // argument_kw_error(GET_THREAD(), iseq, "unknown", keys);
	}
    }

    if (NIL_P(unspecified_bits_value)) {
	unspecified_bits_value = INT2FIX(unspecified_bits);
    }
    // locals[key_num] = unspecified_bits_value;
    return 1;
}

static inline void
args_setup_kw_rest_parameter(VALUE keyword_hash, VALUE *locals)
{
    locals[0] = NIL_P(keyword_hash) ? rb_hash_new() : rb_hash_dup(keyword_hash);
}

static inline void
args_setup_block_parameter(rb_thread_t *th, rb_call_info_t *ci, VALUE *locals)
{
    VALUE blockval = Qnil;
    const rb_block_t *blockptr = ci->blockptr;

    if (blockptr) {
	/* make Proc object */
	if (blockptr->proc == 0) {
	    rb_proc_t *proc;
	    blockval = rb_vm_make_proc(th, blockptr, rb_cProc);
	    GetProcPtr(blockval, proc);
	    ci->blockptr = &proc->block;
	} else {
	    blockval = blockptr->proc;
	}
    }
    *locals = blockval;
}

struct fill_values_arg {
    VALUE *keys;
    VALUE *vals;
    int argc;
};

static int
fill_keys_values(st_data_t key, st_data_t val, st_data_t ptr)
{
    struct fill_values_arg *arg = (struct fill_values_arg *)ptr;
    int i = arg->argc++;
    arg->keys[i] = (VALUE)key;
    arg->vals[i] = (VALUE)val;
    return ST_CONTINUE;
}

static int
setup_parameters_complex(lir_builder_t *builder, rb_thread_t *const th, const rb_iseq_t *const iseq, rb_call_info_t *const ci,
                         VALUE *const locals, const enum arg_setup_type arg_setup_type, jit_snapshot_t *snapshot)
{
    const int min_argc = iseq->param.lead_num + iseq->param.post_num;
    const int max_argc = (iseq->param.flags.has_rest == FALSE) ? min_argc + iseq->param.opt_num : UNLIMITED_ARGUMENTS;
    int i, opt_pc = 0;
    int given_argc;
    struct args_info args_body, *args;
    VALUE keyword_hash = Qnil;
    const int msl = ci->argc + iseq->param.size;
    struct lir_argument lir_arg;
    lir_arg.snapshot = snapshot;

    th->mark_stack_len = msl;

    /* setup args */
    args = &args_body;
    args->ci = ci;
    given_argc = args->argc = ci->argc;
    args->argv = locals;

    if (ci->kw_arg) {
	if (iseq->param.flags.has_kw) {
	    int kw_len = ci->kw_arg->keyword_len;
	    /* copy kw_argv */
	    args->kw_argv = ALLOCA_N(VALUE, kw_len);
	    args->kw_entry = ALLOCA_N(lir_t, kw_len);
	    for (i = 0; i < kw_len; i++) {
		args->kw_entry[kw_len - i] = _TOPN(i);
	    }
	    args->argc -= kw_len;
	    given_argc -= kw_len;
	    MEMCPY(args->kw_argv, locals + args->argc, VALUE, kw_len);
	} else {
	    args->kw_argv = NULL;
	    given_argc = args_kw_argv_to_hash(args);
	}
    } else {
	args->kw_argv = NULL;
    }

    if (ci->flag & VM_CALL_ARGS_SPLAT) {
	args->rest = locals[--args->argc];
	args->rest_index = 0;
	given_argc += RARRAY_LENINT(args->rest) - 1;
    } else {
	args->rest = Qfalse;
    }

    switch (arg_setup_type) {
	case arg_setup_method:
	    break; /* do nothing special */
	case arg_setup_block:
	    if (given_argc == 1 && (min_argc > 0 || iseq->param.opt_num > 1 || iseq->param.flags.has_kw || iseq->param.flags.has_kwrest) && !iseq->param.flags.ambiguous_param0 && args_check_block_arg0(args, th, msl)) {
		given_argc = RARRAY_LENINT(args->rest);
	    }
	    break;
	case arg_setup_lambda:
	    if (given_argc == 1 && given_argc != iseq->param.lead_num && !iseq->param.flags.has_rest && args_check_block_arg0(args, th, msl)) {
		given_argc = RARRAY_LENINT(args->rest);
	    }
    }

    /* argc check */
    if (given_argc < min_argc) {
	if (given_argc == min_argc - 1 && args->kw_argv) {
	    args_stored_kw_argv_to_hash(args);
	    given_argc = args_argc(args);
	} else {
	    if (arg_setup_type == arg_setup_block) {
		// CHECK_VM_STACK_OVERFLOW(th->cfp, min_argc);
		given_argc = min_argc;
		args_extend(args, min_argc);
	    } else {
		argument_arity_error(th, iseq, given_argc, min_argc, max_argc);
	    }
	}
    }

    if (given_argc > min_argc && (iseq->param.flags.has_kw || iseq->param.flags.has_kwrest) && args->kw_argv == NULL) {
	if (args_pop_keyword_hash(args, &keyword_hash, th, msl)) {
	    given_argc--;
	}
    }

    if (given_argc > max_argc && max_argc != UNLIMITED_ARGUMENTS) {
	if (arg_setup_type == arg_setup_block) {
	    /* truncate */
	    args_reduce(args, given_argc - max_argc);
	    given_argc = max_argc;
	} else {
	    argument_arity_error(th, iseq, given_argc, min_argc, max_argc);
	}
    }

    if (iseq->param.flags.has_lead) {
	args_setup_lead_parameters(args, iseq->param.lead_num, locals + 0);
    }

    if (iseq->param.flags.has_post) {
	args_setup_post_parameters(args, iseq->param.post_num, locals + iseq->param.post_start);
    }

    if (iseq->param.flags.has_opt) {
	int opt = args_setup_opt_parameters(args, iseq->param.opt_num, locals + iseq->param.lead_num);
	opt_pc = (int)iseq->param.opt_table[opt];
    }

    if (iseq->param.flags.has_rest) {
	args_setup_rest_parameter(args, locals + iseq->param.rest_start);
    }

    if (iseq->param.flags.has_kw) {
	if (args->kw_argv != NULL) {
	    __asm__ volatile("int3");
	    lir_arg.passed_entry = args->kw_entry;
	    if (args_setup_kw_parameters(builder, &lir_arg, args->ci->kw_arg->keyword_len,
	                                 args->ci->kw_arg->keywords, iseq) == 0) {
		return 1;
	    }
	} else if (!NIL_P(keyword_hash)) {
	    int kw_len = rb_long2int(RHASH_SIZE(keyword_hash));
	    struct fill_values_arg arg;
	    __asm__ volatile("int3");
	    /* copy kw_argv */
	    arg.keys = args->kw_argv = ALLOCA_N(VALUE, kw_len * 2);
	    arg.vals = arg.keys + kw_len;
	    arg.argc = 0;
	    rb_hash_foreach(keyword_hash, fill_keys_values, (VALUE)&arg);
	    assert(arg.argc == kw_len);
	    //FIXME
	    lir_arg.passed_entry = NULL;
	    if (args_setup_kw_parameters(builder, &lir_arg, kw_len, arg.keys, iseq) == 0) {
		return 1;
	    }
	} else {
	    assert(args_argc(args) == 0);
	    lir_arg.passed_entry = NULL;
	    if (args_setup_kw_parameters(builder, &lir_arg, 0, NULL, iseq) == 0) {
		return 1;
	    }
	}
    } else if (iseq->param.flags.has_kwrest) {
	args_setup_kw_rest_parameter(keyword_hash, locals + iseq->param.keyword->rest_start);
    }

    if (iseq->param.flags.has_block) {
	args_setup_block_parameter(th, ci, locals + iseq->param.block_start);
    }

#if 0
    {
	int i;
	for (i=0; i<iseq->param.size; i++) {
	    fprintf(stderr, "local[%d] = %p\n", i, (void *)locals[i]);
	}
    }
#endif

    th->mark_stack_len = 0;

    return 0;
}

static void
raise_argument_error(rb_thread_t *th, const rb_iseq_t *iseq, const VALUE exc)
{
    VALUE at;

    if (iseq) {
	jit_vm_push_frame(th, iseq, VM_FRAME_MAGIC_METHOD, Qnil /* self */, Qnil /* klass */, Qnil /* specval*/,
	                  iseq->iseq_encoded, th->cfp->sp, 0 /* local_size */, 0 /* me */, 0 /* stack_max */);
	at = rb_vm_backtrace_object();
	jit_vm_pop_frame(th);
    } else {
	at = rb_vm_backtrace_object();
    }

    rb_iv_set(exc, "bt_locations", at);
    rb_funcall(exc, rb_intern("set_backtrace"), 1, at);
    rb_exc_raise(exc);
}

static inline VALUE
jit_rb_arity_error_new(int argc, int min, int max)
{
    VALUE err_mess = 0;
    if (min == max) {
	err_mess = rb_sprintf("wrong number of arguments (%d for %d)", argc, min);
    } else if (max == UNLIMITED_ARGUMENTS) {
	err_mess = rb_sprintf("wrong number of arguments (%d for %d+)", argc, min);
    } else {
	err_mess = rb_sprintf("wrong number of arguments (%d for %d..%d)", argc, min, max);
    }
    return rb_exc_new3(rb_eArgError, err_mess);
}

static void
argument_arity_error(rb_thread_t *th, const rb_iseq_t *iseq, const int miss_argc, const int min_argc, const int max_argc)
{
    raise_argument_error(th, iseq, jit_rb_arity_error_new(miss_argc, min_argc, max_argc));
}

static void
argument_kw_error(rb_thread_t *th, const rb_iseq_t *iseq, const char *error, const VALUE keys)
{
    raise_argument_error(th, iseq, rb_keyword_error_new(error, keys));
}

static inline void
vm_caller_setup_arg_splat(lir_builder_t *builder, rb_control_frame_t *cfp, rb_call_info_t *ci)
{
    VALUE *argv = cfp->sp - ci->argc;
    VALUE ary = argv[ci->argc - 1];

    lir_t Rary = _POP();
    jit_event_t *e = current_jit->current_event;
    if (!NIL_P(ary)) {
	long len = RARRAY_LEN(ary), i;
	EmitIR(GuardTypeArray, REG_PC, Rary);
	EmitIR(GuardArraySize, REG_PC, Rary, len);
	for (i = 0; i < len; i++) {
	    lir_t Ridx = emit_load_const(builder, LONG2FIX(i));
	    _PUSH(EmitIR(ArrayGet, Rary, Ridx));
	}
	ci->argc += i - 1;
    }
}

static inline void
vm_caller_setup_arg_kw(lir_builder_t *builder, rb_control_frame_t *cfp, rb_call_info_t *ci, lir_t *Rkw_hash)
{
    const VALUE *const passed_keywords = ci->kw_arg->keywords;
    const int kw_len = ci->kw_arg->keyword_len;
    const VALUE h = rb_hash_new();
    VALUE *sp = cfp->sp;
    int i;
    lir_t entry[kw_len * 2];

    __asm__ volatile("int3");
    for (i = kw_len - 1; i > 0; i--) {
	lir_t key = emit_load_const(builder, ID2SYM(passed_keywords[i]));
	entry[i * 2] = key;
	entry[i * 2] = _POP();
    }

    *Rkw_hash = EmitIR(AllocHash, kw_len * 2, entry);
    ci->argc -= kw_len - 1;
}

#define IS_ARGS_SPLAT(ci) ((ci)->flag & VM_CALL_ARGS_SPLAT)
#define IS_ARGS_KEYWORD(ci) ((ci)->kw_arg != NULL)

#define CALLER_SETUP_ARG(builder, cfp, ci)                            \
    do {                                                              \
	if (UNLIKELY(IS_ARGS_SPLAT(ci)))                              \
	    vm_caller_setup_arg_splat((builder), (cfp), (ci));        \
	if (UNLIKELY(IS_ARGS_KEYWORD(ci)))                            \
	    vm_caller_setup_arg_kw((builder), (cfp), (ci), Rkw_hash); \
    } while (0)
