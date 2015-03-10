/**********************************************************************

  jit_codegen.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#define JIT_MAX_COMPILE_TRACE 40
static int global_live_compiled_trace = 0;
static int rujit_check_compiled_code_size(rujit_t *jit, void *ctx, int newid)
{
    // FIXME unload cold code from compiled cache
    assert(global_live_compiled_trace < JIT_MAX_COMPILE_TRACE && "too many compiled trace");
    return newid;
}

typedef struct buffer {
    jit_list_t buf;
} buffer_t;

#define BUFFER_CAPACITY 4096

static buffer_t *buffer_init(buffer_t *buf)
{
    jit_list_init(&buf->buf);
    jit_list_ensure(&buf->buf, BUFFER_CAPACITY / sizeof(uintptr_t));
    return buf;
}

static void buffer_setnull(buffer_t *buf)
{
    unsigned size = buf->buf.size;
    unsigned capacity = buf->buf.capacity * sizeof(uintptr_t);
    assert(size < capacity);
    ((char *)buf->buf.list)[buf->buf.size] = '\0';
}

static int buffer_printf(buffer_t *buf, const char *fmt, va_list ap)
{
    unsigned size = buf->buf.size;
    unsigned capacity = buf->buf.capacity * sizeof(uintptr_t);
    char *ptr = ((char *)buf->buf.list) + size;
    size_t n = vsnprintf(ptr, capacity - size, fmt, ap);
    if (n + size < capacity) {
	buf->buf.size += n;
	return 1;
    } else {
	// data was not copied because buffer is full.
	buffer_setnull(buf);
	return 0;
    }
}

static void buffer_flush(FILE *fp, buffer_t *buf)
{
    if (buf->buf.size) {
	fputs((char *)buf->buf.list, fp);
	buf->buf.size = 0;
    }
}

static void buffer_dispose(buffer_t *buf)
{
    jit_list_delete(&buf->buf);
}

enum cgen_mode {
#ifndef __STRICT_ANSI__
    PROCESS_MODE, // generate native code directly
#endif
    FILE_MODE // generate temporary c-source file
};

#define MAX_SIDE_EXIT 256

typedef struct CGen {
    enum cgen_mode mode;
    buffer_t buf;
    FILE *fp;
    void *hdr;
    const char *path;
    char *cmd;
    unsigned cmd_len;
} CGen;

static void cgen_setup_command(CGen *gen, const char *lib, const char *file)
{
    gen->cmd_len = (unsigned)(strlen(cmd_template) + strlen(lib) + strlen(file));
    gen->cmd = (char *)malloc(gen->cmd_len + 1);
    memset(gen->cmd, 0, gen->cmd_len);
    snprintf(gen->cmd, gen->cmd_len, cmd_template, lib, file);
}

static void cgen_init(rujit_t *jit, struct rujit_backend_t *self)
{
    CGen *gen = (CGen *)malloc(sizeof(CGen));
    buffer_init(&gen->buf);
    self->ctx = (void *)gen;
}

static void cgen_delete(rujit_t *jit, struct rujit_backend_t *self)
{
    CGen *gen = (CGen *)self->ctx;
    buffer_dispose(&gen->buf);
    gen->hdr = NULL;
    free(gen);
}

static int cgen_freeze(CGen *gen, lir_func_t *func)
{
    int success = 0;
    buffer_flush(gen->fp, &gen->buf);
    // buffer_dispose(&gen->buf);
    JIT_PROFILE_ENTER("nativecode generation");
    if (gen->mode == FILE_MODE) {
	char fpath[512] = {};
	snprintf(fpath, 512, "/tmp/ruby_jit.%d.%d.c", getpid(), func->id);
	cgen_setup_command(gen, gen->path, fpath);

	if (JIT_DUMP_COMPILE_LOG > 1) {
	    fprintf(stderr, "compiling c code : %s\n", gen->cmd);
	}
	if (JIT_DUMP_COMPILE_LOG > 0) {
	    fprintf(stderr, "generated c-code is %s\n", gen->path);
	}
	fclose(gen->fp);
#ifndef __STRICT_ANSI__
	gen->fp = popen(gen->cmd, "w");
#else
#endif
    }
#ifndef __STRICT_ANSI__
    success = pclose(gen->fp);
#else
    success = system(gen->cmd);
#endif
    JIT_PROFILE_LEAVE("nativecode generation", JIT_DUMP_COMPILE_LOG > 0);
    if (gen->cmd_len > 0) {
	gen->cmd_len = 0;
	free(gen->cmd);
	gen->cmd = NULL;
    }
    gen->fp = NULL;
    return success;
}

static void cgen_compile2(CGen *gen, lir_builder_t *builder, lir_func_t *func);
static int cgen_get_function(CGen *gen, lir_func_t *func, native_func_t *nfunc, jit_trace_t *trace);

static native_func_t *cgen_compile(rujit_t *jit, void *ctx, lir_func_t *func, jit_trace_t *trace)
{
    CGen *gen = (CGen *)ctx;
    char path[128] = {};
    int id = rujit_check_compiled_code_size(jit, ctx, func->id);
    native_func_t *nfunc = NULL;

    dump_lir_func(func);
#if 0 && !defined(__STRICT_ANSI__)
    gen->mode = PROCESS_MODE;
#else
    gen->mode = FILE_MODE;
#endif

    gen->hdr = NULL;
    JIT_PROFILE_ENTER("c-code generation");
#ifndef __STRICT_ANSI__
    if (gen->mode == PROCESS_MODE) {
	cgen_setup_command(gen, path, "-");
	gen->fp = popen(gen->cmd, "w");
    } else
#endif
    {
	char fpath[512] = {};
	snprintf(fpath, 512, "/tmp/ruby_jit.%d.%d.c", getpid(), id);
	gen->fp = fopen(fpath, "w");
    }
    snprintf(path, 128, "/tmp/ruby_jit.%d.%d.so", (unsigned)getpid(), id);
    gen->path = path;

    cgen_compile2(gen, &jit->builder, func);
    JIT_PROFILE_LEAVE("c-code generation", JIT_DUMP_COMPILE_LOG > 0);

    if (cgen_freeze(gen, func) == 0) {
	nfunc = native_func_new(func);
	if (cgen_get_function(gen, func, nfunc, trace)) {
	    // compile finished
	    global_live_compiled_trace++;
	}
    }
    return nfunc;
}

static void cgen_unload(rujit_t *jit, void *ctx, native_func_t *func)
{
    global_live_compiled_trace--;
    assert(func->handler != NULL);
    dlclose(func->handler);
    func->handler = NULL;
    func->code = NULL;
#ifdef ENABLE_PROFILE_TRACE_JIT
    fprintf(stderr, "func "
#if JIT_DEBUG_TRACE
                    "%s"
#else
                    "%p"
#endif
                    " invoke count = %ld\n",
#if JIT_DEBUG_TRACE
            func->func_name,
#else
            func,
#endif
            func->invoked);
#endif
#if JIT_DEBUG_TRACE
    if (func->func_name) {
	free(func->func_name);
	func->func_name = NULL;
    }
#endif
}

static void cgen_printf(CGen *gen, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void cgen_printf(CGen *gen, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    if (buffer_printf(&gen->buf, fmt, ap) == 0) {
	buffer_flush(gen->fp, &gen->buf);
	vfprintf(gen->fp, fmt, ap2);
    }
    va_end(ap);
}

static int cgen_get_function(CGen *gen, lir_func_t *func, native_func_t *nfunc, jit_trace_t *trace)
{
    char fname[128] = {};
    snprintf(fname, 128, "ruby_jit_%d", func->id);
#if JIT_DEBUG_TRACE
    nfunc->func_name = strdup(fname);
#endif

    if (gen->hdr == NULL) {
	gen->hdr = dlopen(gen->path, RTLD_LAZY);
    }
    if (gen->hdr != NULL) {
	char fname2[128] = {};
	snprintf(fname2, 128, "init_%s", fname);
	if (trace) {
	    trace_side_exit_handler_t **side_exits;
	    // trace-jit mode
	    int (*finit)(const void *, jit_trace_t *, trace_side_exit_handler_t **);
	    finit = dlsym(gen->hdr, fname2);
	    if (!finit)
		return 0;
	    side_exits = (trace_side_exit_handler_t **)&trace->exit_handlers.list[0];
	    finit(&jit_runtime, trace, side_exits);
	} else {
	    // method-jit mode
	    int (*finit)(const void *local_jit_runtime, native_func_t *nfunc);
	    finit = dlsym(gen->hdr, fname2);
	    if (!finit)
		return 0;
	    finit(&jit_runtime, nfunc);
	}
	nfunc->code = dlsym(gen->hdr, fname);
	nfunc->handler = gen->hdr;
	return 1;
    }
    return 0;
}

#define EMIT_CODE(GEN, OP, VAL, LHS, RHS)                      \
    cgen_printf(gen, "v%d = rb_jit_exec_" #OP "(v%d, v%d);\n", \
                (VAL), lir_getid(LHS), lir_getid(RHS))

#define EMIT_CODE1(GEN, OP, VAL, ARG)                     \
    cgen_printf(gen, "v%d = rb_jit_exec_" #OP "(v%d);\n", \
                (VAL), lir_getid(ARG))

#define EMIT_GUARD(GEN, CODE)                                                                                 \
    do {                                                                                                      \
	IGuardTypeFixnum *ir = (IGuardTypeFixnum *)Inst;                                                      \
	uintptr_t exit_block_id = (uintptr_t)ir->Exit;                                                        \
	cgen_printf(gen, "if(!(" CODE ")) {\n" "  goto L_exit%ld;\n" "}\n", lir_getid(ir->R), exit_block_id); \
    } while (0)

static void compile_inst(CGen *gen, lir_t Inst)
{
    int Id = lir_getid(Inst);
    switch (lir_opcode(Inst)) {
	case OPCODE_IGuardTypeSymbol: {
	    EMIT_GUARD(gen, "SYMBOL_P(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeFixnum: {
	    EMIT_GUARD(gen, "FIXNUM_P(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeBignum: {
	    EMIT_GUARD(gen, "RB_TYPE_P(v%d, T_BIGNUM)");
	    break;
	}
	case OPCODE_IGuardTypeFloat: {
	    EMIT_GUARD(gen, "RB_FLOAT_TYPE_P(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeSpecialConst: {
	    EMIT_GUARD(gen, "SPECIAL_CONST_P(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeNonSpecialConst: {
	    EMIT_GUARD(gen, "!SPECIAL_CONST_P(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeArray: {
	    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cArray");
	    break;
	}
	case OPCODE_IGuardTypeString: {
	    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cString");
	    break;
	}
	case OPCODE_IGuardTypeHash: {
	    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cHash");
	    break;
	}
	case OPCODE_IGuardTypeRegexp: {
	    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cRegexp");
	    break;
	}
	//case OPCODE_IGuardTypeTime: {
	//    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cTime");
	//    break;
	//}
	case OPCODE_IGuardTypeMath: {
	    EMIT_GUARD(gen, "RBASIC_CLASS(v%d) == local_jit_runtime->cMath");
	    break;
	}
	case OPCODE_IGuardTypeObject: {
	    EMIT_GUARD(gen, "RB_TYPE_P(v%d, T_OBJECT)");
	    break;
	}
	case OPCODE_IGuardTypeNil: {
	    EMIT_GUARD(gen, "RTEST(v%d)");
	    break;
	}
	case OPCODE_IGuardTypeNonNil: {
	    EMIT_GUARD(gen, "!RTEST(v%d)");
	    break;
	}
	case OPCODE_IGuardBlockEqual: {
	    IGuardBlockEqual *ir = (IGuardBlockEqual *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(gen, "{\n"
	                     "  rb_block_t *block = (rb_block_t *) v%d;\n"
	                     "  const rb_iseq_t *iseq = (const rb_iseq_t *) %p;\n"
	                     "  if(!(block->iseq == iseq)) {\n"
	                     "    goto L_exit%ld;\n"
	                     "  }\n"
	                     "}\n",
	                lir_getid(ir->R), ir->iseq, exit_block_id);
	    break;
	}
	case OPCODE_IGuardProperty: {
	    IGuardProperty *ir = (IGuardProperty *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(
	        gen, "if(!(RCLASS_SERIAL(RBASIC(v%d)->klass) == 0x%llx)) {\n"
	             "  goto L_exit%ld;\n"
	             "}\n",
	        lir_getid(ir->R), ir->serial, exit_block_id);
	    break;
	}
	case OPCODE_IGuardArraySize: {
	    IGuardArraySize *ir = (IGuardArraySize *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(
	        gen,
	        "{\n"
	        "  long len = RARRAY_LEN(v%d);\n"
	        "  if (!(len == %ld)) {\n"
	        "    goto L_exit%ld;\n"
	        "  }\n"
	        "}\n",
	        lir_getid(ir->R), ir->size, exit_block_id);

	    break;
	}
	case OPCODE_IGuardClassMethod: {
	    IGuardClassMethod *ir = (IGuardClassMethod *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(
	        gen,
	        "{ CALL_INFO ci = (CALL_INFO) %p;\n"
	        "  if (!(JIT_GET_GLOBAL_METHOD_STATE() == ci->method_state &&\n"
	        "       RCLASS_SERIAL(v%d) == ci->class_serial)) {\n"
	        "    goto L_exit%ld;\n"
	        "  }\n"
	        "}\n",
	        ir->ci, lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardMethodCache: {
	    IGuardMethodCache *ir = (IGuardMethodCache *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(
	        gen,
	        "{ CALL_INFO ci = (CALL_INFO) %p;\n"
	        "  if (!(JIT_GET_GLOBAL_METHOD_STATE() == ci->method_state &&\n"
	        "       RCLASS_SERIAL(CLASS_OF(v%d)) == ci->class_serial)) {\n"
	        "    goto L_exit%ld;\n"
	        "  }\n"
	        "}\n",
	        ir->ci, lir_getid(ir->R), exit_block_id);
	    break;
	}
	case OPCODE_IGuardMethodRedefine: {
	    IGuardMethodRedefine *ir = (IGuardMethodRedefine *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(gen, "if (!JIT_OP_UNREDEFINED_P(%d, %d)) {\n"
	                     "  goto L_exit%ld;\n"
	                     "}\n",
	                ir->bop, ir->klass_flag, exit_block_id);
	    break;
	}
	case OPCODE_IExit: {
	    IExit *ir = (IExit *)Inst;
	    uintptr_t exit_block_id = (uintptr_t)ir->Exit;
	    cgen_printf(gen, "goto L_exit%ld;\n", exit_block_id);
	    break;
	}
	case OPCODE_IFixnumAdd: {
	    IFixnumAdd *ir = (IFixnumAdd *)Inst;
	    EMIT_CODE(gen, IFixnumAdd, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumSub: {
	    IFixnumSub *ir = (IFixnumSub *)Inst;
	    EMIT_CODE(gen, IFixnumSub, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumMul: {
	    IFixnumMul *ir = (IFixnumMul *)Inst;
	    EMIT_CODE(gen, IFixnumMul, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumDiv: {
	    IFixnumDiv *ir = (IFixnumDiv *)Inst;
	    EMIT_CODE(gen, IFixnumDiv, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	case OPCODE_IFixnumMod: {
	    IFixnumMod *ir = (IFixnumMod *)Inst;
	    EMIT_CODE(gen, IFixnumMod, Id, ir->LHS, ir->RHS);
	    assert(0 && "need to test");
	    break;
	}
	//case OPCODE_IFixnumPow: {
	//    IFixnumPow *ir = (IFixnumPow *)Inst;
	//    assert(0 && "need to implement");
	//    //DBL2NUM(pow(RFLOAT_VALUE(x), (double)FIX2LONG(y)));
	//    break;
	//}
	case OPCODE_IFixnumAddOverflow: {
	    IFixnumAddOverflow *ir = (IFixnumAddOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumAddOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumSubOverflow: {
	    IFixnumSubOverflow *ir = (IFixnumSubOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumSubOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumMulOverflow: {
	    IFixnumMulOverflow *ir = (IFixnumMulOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumMulOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumDivOverflow: {
	    IFixnumDivOverflow *ir = (IFixnumDivOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumModOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumModOverflow: {
	    IFixnumModOverflow *ir = (IFixnumModOverflow *)Inst;
	    EMIT_CODE(gen, IFixnumModOverflow, Id, ir->LHS, ir->RHS);
	    break;
	}
	//case OPCODE_IFixnumPowOverflow: {
	//    IFixnumPowOverflow *ir = (IFixnumPowOverflow *)Inst;
	//    assert(0 && "need to implement");
	//    //DBL2NUM(pow(RFLOAT_VALUE(x), (double)FIX2LONG(y)));
	//    break;
	//}
	case OPCODE_IFixnumEq: {
	    IFixnumEq *ir = (IFixnumEq *)Inst;
	    EMIT_CODE(gen, IFixnumEq, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumNe: {
	    IFixnumNe *ir = (IFixnumNe *)Inst;
	    EMIT_CODE(gen, IFixnumNe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumGt: {
	    IFixnumGt *ir = (IFixnumGt *)Inst;
	    EMIT_CODE(gen, IFixnumGt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumGe: {
	    IFixnumGe *ir = (IFixnumGe *)Inst;
	    EMIT_CODE(gen, IFixnumGe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumLt: {
	    IFixnumLt *ir = (IFixnumLt *)Inst;
	    EMIT_CODE(gen, IFixnumLt, Id, ir->LHS, ir->RHS);
	    // cgen_printf(gen, "fprintf(stderr, \"v%d=%%ld < v%d=%%ld\\n\", FIX2LONG(v%d), FIX2LONG(v%d));\n", lir_getid(ir->LHS), lir_getid(ir->RHS), lir_getid(ir->LHS), lir_getid(ir->RHS));
	    break;
	}
	case OPCODE_IFixnumLe: {
	    IFixnumLe *ir = (IFixnumLe *)Inst;
	    EMIT_CODE(gen, IFixnumLe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumAnd: {
	    IFixnumAnd *ir = (IFixnumAnd *)Inst;
	    EMIT_CODE(gen, IFixnumAnd, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumOr: {
	    IFixnumOr *ir = (IFixnumOr *)Inst;
	    EMIT_CODE(gen, IFixnumOr, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumXor: {
	    IFixnumXor *ir = (IFixnumXor *)Inst;
	    EMIT_CODE(gen, IFixnumXor, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumLshift: {
	    IFixnumLshift *ir = (IFixnumLshift *)Inst;
	    EMIT_CODE(gen, IFixnumLshift, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumRshift: {
	    IFixnumRshift *ir = (IFixnumRshift *)Inst;
	    EMIT_CODE(gen, IFixnumRshift, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFixnumNeg: {
	    IFixnumNeg *ir = (IFixnumNeg *)Inst;
	    EMIT_CODE1(gen, IFixnumNeg, Id, ir->Recv);
	    break;
	}

	case OPCODE_IFixnumComplement: {
	    IFixnumComplement *ir = (IFixnumComplement *)Inst;
	    EMIT_CODE1(gen, IFixnumComplement, Id, ir->Recv);
	    break;
	}
	case OPCODE_IFloatAdd: {
	    IFloatAdd *ir = (IFloatAdd *)Inst;
	    EMIT_CODE(gen, IFloatAdd, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatSub: {
	    IFloatSub *ir = (IFloatSub *)Inst;
	    EMIT_CODE(gen, IFloatSub, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatMul: {
	    IFloatMul *ir = (IFloatMul *)Inst;
	    EMIT_CODE(gen, IFloatMul, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatDiv: {
	    IFloatDiv *ir = (IFloatDiv *)Inst;
	    EMIT_CODE(gen, IFloatDiv, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatMod: {
	    IFloatMod *ir = (IFloatMod *)Inst;
	    EMIT_CODE(gen, IFloatMod, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatPow: {
	    IFloatPow *ir = (IFloatPow *)Inst;
	    EMIT_CODE(gen, IFloatPow, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatNeg: {
	    IFloatNeg *ir = (IFloatNeg *)Inst;
	    EMIT_CODE1(gen, IFloatNeg, Id, ir->Recv);
	    break;
	}
	case OPCODE_IFloatEq: {
	    IFloatEq *ir = (IFloatEq *)Inst;
	    EMIT_CODE(gen, IFloatEq, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatNe: {
	    IFloatNe *ir = (IFloatNe *)Inst;
	    EMIT_CODE(gen, IFloatNe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatGt: {
	    IFloatGt *ir = (IFloatGt *)Inst;
	    EMIT_CODE(gen, IFloatGt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatGe: {
	    IFloatGe *ir = (IFloatGe *)Inst;
	    EMIT_CODE(gen, IFloatGe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatLt: {
	    IFloatLt *ir = (IFloatLt *)Inst;
	    EMIT_CODE(gen, IFloatLt, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IFloatLe: {
	    IFloatLe *ir = (IFloatLe *)Inst;
	    EMIT_CODE(gen, IFloatLe, Id, ir->LHS, ir->RHS);
	    break;
	}
	case OPCODE_IObjectClass: {
	    IObjectClass *ir = (IObjectClass *)Inst;
	    cgen_printf(gen, "v%d = CLASS_OF(v%d);\n", Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IObjectNot: {
	    IObjectNot *ir = (IObjectNot *)Inst;
	    cgen_printf(gen, "v%d = (RTEST(v%d)) ? Qfalse : Qtrue;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	//case OPCODE_IObjectEq: {
	//    IObjectNot *ir = (IObjectNot *)Inst;
	//    cgen_printf(gen, "v%d = (RTEST(v%d) == 0) ? Qtrue : Qfalse;\n",
	//                Id, lir_getid(ir->Recv));
	//    break;
	//}
	//case OPCODE_IObjectNe: {
	//    IObjectNot *ir = (IObjectNot *)Inst;
	//    cgen_printf(gen, "v%d = (RTEST(v%d) == 0) ? Qtrue : Qfalse;\n",
	//                Id, lir_getid(ir->Recv));
	//    break;
	//}
	case OPCODE_IObjectToString: {
	    IObjectToString *ir = (IObjectToString *)Inst;
	    EMIT_CODE1(gen, IObjectToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IFixnumToFloat: {
	    IFixnumToFloat *ir = (IFixnumToFloat *)Inst;
	    EMIT_CODE1(gen, IFixnumToFloat, Id, ir->Val);
	    break;
	}
	case OPCODE_IFixnumToString: {
	    IFixnumToString *ir = (IFixnumToString *)Inst;
	    EMIT_CODE1(gen, IFixnumToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IFloatToFixnum: {
	    IFloatToFixnum *ir = (IFloatToFixnum *)Inst;
	    EMIT_CODE1(gen, IFloatToFixnum, Id, ir->Val);
	    break;
	}
	case OPCODE_IFloatToString: {
	    IFloatToString *ir = (IFloatToString *)Inst;
	    assert(0 && "need to implement flo_to_s");
	    EMIT_CODE1(gen, IFloatToString, Id, ir->Val);
	    break;
	}
	case OPCODE_IStringToFixnum: {
	    IStringToFixnum *ir = (IStringToFixnum *)Inst;
	    cgen_printf(gen, "v%d = rb_str_to_inum(v%d, 10, 0);\n", Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IStringToFloat: {
	    IStringToFloat *ir = (IStringToFloat *)Inst;
	    cgen_printf(gen, "v%d = rb_str_to_dbl(v%d, 0);\n", Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IMathSin: {
	    IMathSin *ir = (IMathSin *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(sin(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathCos: {
	    IMathCos *ir = (IMathCos *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(cos(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathTan: {
	    IMathTan *ir = (IMathTan *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(tan(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathExp: {
	    IMathExp *ir = (IMathExp *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(exp(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathSqrt: {
	    IMathSqrt *ir = (IMathSqrt *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(sqrt(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathLog10: {
	    IMathLog10 *ir = (IMathLog10 *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(log10(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IMathLog2: {
	    IMathLog2 *ir = (IMathLog2 *)Inst;
	    cgen_printf(gen, "v%d = DBL2NUM(log2(RFLOAT_VALUE(v%d)));\n", Id,
	                lir_getid(ir->arg));
	    break;
	}
	case OPCODE_IStringLength: {
	    IStringLength *ir = (IStringLength *)Inst;
	    cgen_printf(gen, "v%d = rb_str_length(v%d);\n", Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IStringEmptyP: {
	    IStringEmptyP *ir = (IStringEmptyP *)Inst;
	    cgen_printf(gen, "v%d = (RSTRING_LEN(v%d) == 0) ? Qtrue : Qfalse;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IStringConcat: {
	    IStringConcat *ir = (IStringConcat *)Inst;
	    cgen_printf(gen, "v%d = rb_str_plus(v%d, v%d);\n", Id,
	                lir_getid(ir->LHS),
	                lir_getid(ir->RHS));
	    break;
	}
	case OPCODE_IStringAdd: {
	    IStringAdd *ir = (IStringAdd *)Inst;
	    EMIT_CODE(gen, IStringAdd, Id, ir->Recv, ir->Obj);
	    break;
	}
	case OPCODE_IArrayLength: {
	    IArrayLength *ir = (IArrayLength *)Inst;
	    cgen_printf(gen, "v%d = LONG2NUM(RARRAY_LEN(v%d));\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IArrayEmptyP: {
	    IArrayEmptyP *ir = (IArrayEmptyP *)Inst;
	    cgen_printf(gen, "v%d = (RARRAY_LEN(v%d) == 0) ? Qtrue : Qfalse;\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IArrayDup: {
	    IArrayDup *ir = (IArrayDup *)Inst;
	    cgen_printf(gen, "v%d = rb_ary_resurrect(v%d);\n",
	                Id, lir_getid(ir->orig));
	    break;
	}
	case OPCODE_IArrayAdd: {
	    IArrayAdd *ir = (IArrayAdd *)Inst;
	    cgen_printf(gen, "v%d = rb_ary_push(v%d, v%d);\n",
	                Id, lir_getid(ir->Recv),
	                lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IArrayConcat: {
	    IArrayConcat *ir = (IArrayConcat *)Inst;
	    cgen_printf(gen, "v%d = rb_ary_plus(v%d, v%d);\n",
	                Id, lir_getid(ir->LHS),
	                lir_getid(ir->RHS));
	    break;
	}
	case OPCODE_IArrayGet: {
	    IArrayGet *ir = (IArrayGet *)Inst;
	    cgen_printf(gen, "v%d = rb_ary_entry(v%d, FIX2LONG(v%d));\n", Id,
	                lir_getid(ir->Recv), lir_getid(ir->Index));
	    break;
	}
	case OPCODE_IArraySet: {
	    IArraySet *ir = (IArraySet *)Inst;
	    cgen_printf(gen, "rb_ary_store(v%d, FIX2LONG(v%d), v%d);\n"
	                     "v%d = v%d;\n",
	                lir_getid(ir->Recv),
	                lir_getid(ir->Index),
	                lir_getid(ir->Val), Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IHashLength: {
	    IHashLength *ir = (IHashLength *)Inst;
	    cgen_printf(gen, "v%d = INT2FIX(RHASH_SIZE(v%d));\n",
	                Id, lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IHashEmptyP: {
	    IHashEmptyP *ir = (IHashEmptyP *)Inst;
	    cgen_printf(gen,
	                "v%d = (RHASH_EMPTY_P(v%d) == 0) ? Qtrue : Qfalse;\n", Id,
	                lir_getid(ir->Recv));
	    break;
	}
	case OPCODE_IHashGet: {
	    IHashGet *ir = (IHashGet *)Inst;
	    cgen_printf(gen, "  v%d = rb_hash_aref(v%d, v%d);\n",
	                Id, lir_getid(ir->Recv),
	                lir_getid(ir->Index));
	    break;
	}
	case OPCODE_IHashSet: {
	    IHashSet *ir = (IHashSet *)Inst;
	    cgen_printf(gen, "  rb_hash_aset(v%d, v%d, v%d);\n"
	                     "  v%d = v%d;\n",
	                lir_getid(ir->Recv), lir_getid(ir->Index),
	                lir_getid(ir->Val), Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IRegExpMatch: {
	    IRegExpMatch *ir = (IRegExpMatch *)Inst;
	    cgen_printf(gen, "  v%d = rb_reg_match(v%d, v%d);\n", Id,
	                lir_getid(ir->Re),
	                lir_getid(ir->Str));
	    break;
	}
	case OPCODE_IAllocObject: {
	    IAllocObject *ir = (IAllocObject *)Inst;
	    cgen_printf(gen, "  v%d = rb_obj_alloc(v%d);\n",
	                Id, lir_getid(ir->Klass));
	    break;
	}
	case OPCODE_IAllocArray: {
	    IAllocArray *ir = (IAllocArray *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  long num = %d;\n"
	                     "  VALUE argv[%d];\n",
	                ir->argc, ir->argc);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "argv[%d] = v%d;\n", i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  v%d = rb_ary_new4(num, argv);\n"
	                     "}\n",
	                Id);
	    break;
	}
	case OPCODE_IAllocHash: {
	    IAllocHash *ir = (IAllocHash *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  VALUE val = rb_hash_new();\n");
	    for (i = 0; i < ir->argc; i += 2) {
		cgen_printf(gen, "rb_hash_aset(val, v%d, v%d);\n",
		            lir_getid(ir->argv[i]),
		            lir_getid(ir->argv[i + 1]));
	    }
	    cgen_printf(gen, "  v%d = val;\n"
	                     "}\n",
	                Id);

	    break;
	}
	case OPCODE_IAllocString: {
	    IAllocString *ir = (IAllocString *)Inst;
	    cgen_printf(gen, "  v%d = rb_str_resurrect(v%d);\n",
	                Id, lir_getid(ir->OrigStr));
	    break;
	}
	case OPCODE_IAllocRange: {
	    IAllocRange *ir = (IAllocRange *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  long flag = %d;\n"
	                     "  VALUE low  = v%d;\n"
	                     "  VALUE high = v%d;\n"
	                     "  v%d = rb_range_new(low, high, flag);\n"
	                     "}\n",
	                ir->Flag, lir_getid(ir->Low), lir_getid(ir->High), Id);

	    break;
	}
	case OPCODE_IAllocRegexFromArray: {
	    IAllocRegexFromArray *ir = (IAllocRegexFromArray *)Inst;
	    cgen_printf(gen, "  v%d = rb_reg_new_ary(v%d, %d);\n",
	                Id, lir_getid(ir->Ary), ir->opt);
	    break;
	}
	case OPCODE_IAllocBlock: {
	    IAllocBlock *ir = (IAllocBlock *)Inst;
	    // cgen_printf(gen, "__int3__;\n");
	    cgen_printf(gen,
	                "{\n"
	                "  rb_block_t *blockptr = (rb_block_t *)v%d;\n"
	                "  rb_proc_t *proc;\n"
	                "  VALUE blockval = rb_vm_make_proc(th, blockptr, local_jit_runtime->cProc);\n"
	                "  GetProcPtr(blockval, proc);\n"
	                "  v%d = (VALUE)&proc->block;\n"
	                "}\n",
	                lir_getid(ir->block), Id);
	    break;
	}
	case OPCODE_IGetGlobal: {
	    IGetGlobal *ir = (IGetGlobal *)Inst;
	    cgen_printf(gen, "  v%d = GET_GLOBAL((struct rb_global_entry *)%p);\n", Id, (struct global_entry *)ir->Entry);
	    break;
	}
	case OPCODE_ISetGlobal: {
	    ISetGlobal *ir = (ISetGlobal *)Inst;
	    cgen_printf(gen, "  SET_GLOBAL((struct rb_global_entry *)%p, v%d);\n", (struct global_entry *)ir->Entry, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IGetPropertyName: {
	    IGetPropertyName *ir = (IGetPropertyName *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  long index = %ld;\n"
	                     "  VALUE  obj = v%d;\n"
	                     "  VALUE *ptr = ROBJECT_IVPTR(obj);\n"
	                     "  v%d = ptr[index];\n"
	                     "}\n",
	                ir->Index, lir_getid(ir->Recv), Id);
	    break;
	}
	case OPCODE_ISetPropertyName: {
	    ISetPropertyName *ir = (ISetPropertyName *)Inst;
	    cgen_printf(gen, "{\n"
	                     "  VALUE  obj = v%d;\n"
	                     "  VALUE  val = v%d;\n",
	                lir_getid(ir->Recv), lir_getid(ir->Val));
	    if (ir->id == 0) {
		cgen_printf(gen, "  VALUE *ptr = ROBJECT_IVPTR(obj);\n"
		                 "  long index = %ld;\n"
		                 "  RB_OBJ_WRITE(obj, &ptr[index], val);\n"
		                 "  ptr[index] = val;\n",
		            ir->Index);
	    } else {
		cgen_printf(gen, "  ID id = (uintptr_t) %lu;\n"
		                 "  rb_ivar_set(obj, id, val);\n",
		            (uintptr_t)ir->id);
	    }
	    cgen_printf(gen, "  v%d = val;\n"
	                     "}\n",
	                Id);
	    break;
	}
	case OPCODE_ILoadSelf: {
	    cgen_printf(gen, "v%d = GET_SELF();\n", Id);
	    break;
	}
	case OPCODE_ILoadSelfAsBlock: {
	    ILoadSelfAsBlock *ir = (ILoadSelfAsBlock *)Inst;
	    cgen_printf(gen,
	                "{\n"
	                "  ISEQ blockiseq = (ISEQ) %p;\n"
	                "  v%d = (VALUE) RUBY_VM_GET_BLOCK_PTR_IN_CFP(reg_cfp);\n"
	                "  /*assert(((rb_block_t *)v%d)->iseq == NULL);*/\n"
	                "  ((rb_block_t *)v%d)->iseq = blockiseq;\n"
	                "}\n",
	                ir->iseq, Id, Id, Id);
	    break;
	}
	case OPCODE_ILoadBlock: {
	    // ILoadBlock *ir = (ILoadBlock *) Inst;
	    cgen_printf(gen, "  v%d = (VALUE) JIT_CF_BLOCK_PTR(reg_cfp);\n", Id);
	    break;
	}
	case OPCODE_ILoadConstNil: {
	    cgen_printf(gen, "v%d = Qnil;\n", Id);
	    break;
	}
	case OPCODE_ILoadConstObject:
	case OPCODE_ILoadConstBoolean:
	case OPCODE_ILoadConstFixnum:
	case OPCODE_ILoadConstFloat:
	case OPCODE_ILoadConstString:
	case OPCODE_ILoadConstRegexp:
	case OPCODE_ILoadConstSpecialObject: {
	    ILoadConstObject *ir = (ILoadConstObject *)Inst;
	    cgen_printf(gen, "v%d = (VALUE) 0x%lx;\n", Id, ir->Val);
	    break;
	}
	case OPCODE_IEnvStore: {
	    IEnvStore *ir = (IEnvStore *)Inst;
	    if (ir->Level > 0) {
		cgen_printf(gen, "{\n"
		                 "  int i, lev = (int)%d;\n"
		                 "  VALUE *ep = GET_EP();\n"
		                 "\n"
		                 "  for (i = 0; i < lev; i++) {\n"
		                 "      ep = GET_PREV_EP(ep);\n"
		                 "  }\n"
		                 "  *(ep - %d) = v%d;\n"
		                 "}\n",
		            ir->Level, ir->Index, lir_getid(ir->Val));
	    } else {
		cgen_printf(gen, "  *(GET_EP() - %d) = v%d;\n", ir->Index,
		            lir_getid(ir->Val));
	    }
	    cgen_printf(gen, "v%d = v%d;\n", Id, lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IEnvLoad: {
	    IEnvLoad *ir = (IEnvLoad *)Inst;
	    if (ir->Level > 0) {
		cgen_printf(gen, "{\n"
		                 "  int i, lev = (int)%d;\n"
		                 "  VALUE *ep = GET_EP();\n"
		                 "\n"
		                 "  for (i = 0; i < lev; i++) {\n"
		                 "      ep = GET_PREV_EP(ep);\n"
		                 "  }\n"
		                 "  v%d = *(ep - %d);\n"
		                 "}\n",
		            ir->Level, Id, ir->Index);
	    } else {
		cgen_printf(gen, "  v%d = *(GET_EP() - %d);\n", Id, ir->Index);
	    }
	    break;
	}
	case OPCODE_IStackPush: {
	    IStackPush *ir = (IStackPush *)Inst;
	    cgen_printf(gen, "  PUSH(v%d);\n", lir_getid(ir->Val));
	    break;
	}
	case OPCODE_IStackPop: {
	    cgen_printf(gen, "  v%d = *POP();\n", Id);
	    break;
	}
	case OPCODE_ICallMethod: {
	    ICallMethod *ir = (ICallMethod *)Inst;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  VALUE *basesp = GET_SP();\n"
	                     "  CALL_INFO ci = (CALL_INFO) %p;\n",
	                ir->ci);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%d;\n",
		            i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(
	        gen,
	        "SET_SP(basesp + %d);\n"
	        "  ci->recv = v%d;\n"
	        "  v%d = (*(ci)->call)(th, GET_CFP(), (ci));\n"
	        "  /*assert(v%d != Qundef && \"method must c-defined method\");*/\n"
	        "  SET_SP(basesp);\n"
	        "}\n",
	        ir->argc, lir_getid(ir->argv[0]), Id, Id);
	    break;
	}
	case OPCODE_IInvokeMethod: {
	    IInvokeMethod *ir = (IInvokeMethod *)Inst;
	    lir_func_t *func = ir->func;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  CALL_INFO ci = (CALL_INFO) %p;\n"
	                     "  SET_PC((VALUE *) %p + %d);\n",
	                ir->ci, func->pc, insn_len(BIN(opt_send_without_block)));

	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%d;\n",
		            i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  SET_SP(GET_SP() + %d);\n"
	                     "  ci->argc = %d;\n"
	                     "  ci->recv = v%d;\n",
	                ir->argc, ir->argc - 1, lir_getid(ir->argv[0]));
	    if (ir->block != 0) {
		cgen_printf(gen, "  ci->blockptr = (rb_block_t *) v%d;\n"
		                 "  assert(ci->blockptr != 0);\n"
		                 "  ci->blockptr->iseq = ci->blockiseq;\n"
		                 "  ci->blockptr->proc = 0;\n",
		            lir_getid(ir->block));
	    }
	    cgen_printf(gen,
	                "  jit_vm_call_iseq_setup_normal(th, reg_cfp, ci);\n"
	                "  reg_cfp = th->cfp;\n");
	    cgen_printf(gen, "}\n");
	    break;
	}
	case OPCODE_IInvokeBlock: {
	    IInvokeBlock *ir = (IInvokeBlock *)Inst;
	    lir_func_t *func = ir->func;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  CALL_INFO ci = (CALL_INFO) %p;\n"
	                     "  SET_PC((VALUE *) %p + %d);\n",
	                ir->ci, func->pc, insn_len(BIN(invokeblock)));

	    for (i = 1; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%d;\n",
		            i - 1, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  SET_SP(GET_SP() + %d);\n", ir->argc - 1);
	    cgen_printf(gen,
	                "  ci->argc = ci->orig_argc;\n"
	                "  ci->blockptr = 0;\n"
	                "  ci->recv = v%d;\n"
	                "  jit_vm_call_block_setup(th, reg_cfp,\n"
	                "                          (rb_block_t *) v%d, ci, %d);\n"
	                "  reg_cfp = th->cfp;\n",
	                lir_getid(ir->argv[0]), lir_getid(ir->block), ir->argc - 1);
	    cgen_printf(gen, "}\n");
	    break;
	}
	case OPCODE_IInvokeNative: {
	    int i;
	    IInvokeNative *ir = (IInvokeNative *)Inst;
	    cgen_printf(gen,
	                "{\n"
	                "    VALUE *basesp = GET_SP();\n"
	                "    CALL_INFO ci = (CALL_INFO)%p;\n",
	                ir->ci);
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%d;\n",
		            i, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "SET_SP(basesp + %d);\n", ir->argc);
	    cgen_printf(gen,
	                "    jit_vm_push_frame(th, 0, VM_FRAME_MAGIC_CFUNC, ci->recv, ci->defined_class,\n"
	                "            VM_ENVVAL_BLOCK_PTR(ci->blockptr), 0, GET_SP(), 1, ci->me, 0);\n"
	                "    reg_cfp->sp -= %d + 1;\n",
	                ir->ci->argc);
	    cgen_printf(gen, "  v%d = ((jit_native_func%d_t)%p)(", Id, ir->argc, ir->fptr);
	    for (i = 0; i < ir->argc; i++) {
		if (i != 0) {
		    cgen_printf(gen, ", ");
		}
		cgen_printf(gen, "v%d", lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, ");\n"
	                     "    jit_vm_pop_frame(th);\n"
	                     "    reg_cfp = th->cfp;\n"
	                     "}\n");
	    break;
	}
	case OPCODE_IInvokeConstructor: {
	    IInvokeConstructor *ir = (IInvokeConstructor *)Inst;
	    lir_func_t *func = ir->func;
	    int i;
	    cgen_printf(gen, "{\n"
	                     "  CALL_INFO ci = (CALL_INFO) %p;\n"
	                     "  SET_PC((VALUE *) %p + %d);\n",
	                ir->ci, func->pc, insn_len(BIN(opt_send_without_block)));

	    cgen_printf(gen, "(GET_SP())[0] = v%d;\n", lir_getid(ir->recv));
	    for (i = 0; i < ir->argc; i++) {
		cgen_printf(gen, "(GET_SP())[%d] = v%d;\n",
		            i + 1, lir_getid(ir->argv[i]));
	    }
	    cgen_printf(gen, "  SET_SP(GET_SP() + %d);\n", ir->argc + 1);
	    //    if (ir->block != 0) {
	    // cgen_printf(gen, "  ci->blockptr = (rb_block_t *) v%d;\n"
	    //                  "  assert(ci->blockptr != 0);\n"
	    //                  "  ci->blockptr->iseq = ci->blockiseq;\n"
	    //                  "  ci->blockptr->proc = 0;\n",
	    //             lir_getid(ir->block));
	    //    }
	    cgen_printf(gen,
	                "  ci->recv = v%d;\n"
	                "  jit_vm_call_iseq_setup_normal(th, reg_cfp, ci);\n"
	                "  reg_cfp = th->cfp;\n"
	                "}\n",
	                lir_getid(ir->recv));
	    break;
	}

	case OPCODE_IPatternMatch: {
	    IPatternMatch *ir = (IPatternMatch *)Inst;
	    cgen_printf(
	        gen,
	        "{\n"
	        "  enum vm_check_match_type checkmatch_type ="
	        "       (enum vm_check_match_type) %u;\n"
	        "  VALUE result = Qfalse;\n"
	        "  VALUE pattern = v%d;\n"
	        "  VALUE target  = v%d;\n"
	        "  if (RTEST(local_jit_runtime->_check_match(pattern, target, checkmatch_type))) {\n"
	        "    result = Qtrue;\n"
	        "  }\n"
	        "  v%d = result;\n"
	        "}\n",
	        ir->flag, lir_getid(ir->Pattern), lir_getid(ir->Target), Id);
	    break;
	}

	case OPCODE_IPatternMatchRange: {
	    IPatternMatchRange *ir = (IPatternMatchRange *)Inst;
	    cgen_printf(
	        gen, "{\n"
	             "  int i;\n"
	             "  enum vm_check_match_type checkmatch_type =\n"
	             "       (enum vm_check_match_type) %u\n\n"
	             "  VALUE result = Qfalse;\n"
	             "  VALUE pattern = v%d;\n"
	             "  VALUE target  = v%d;\n"
	             "  for (i = 0; i < RARRAY_LEN(pattern); i++) {\n"
	             "    if (RTEST(local_jit_runtime->_check_match(RARRAY_AREF(pattern, i), target,\n"
	             "                          checkmatch_type))) {\n"
	             "      result = Qtrue;\n"
	             "      break;\n"
	             "    }\n"
	             "  }\n"
	             "  v%d = result;\n"
	             "}\n",
	        ir->flag, lir_getid(ir->Pattern), lir_getid(ir->Target), Id);
	    break;
	}
	case OPCODE_IJump: {
	    IJump *ir = (IJump *)Inst;
	    cgen_printf(gen, "goto L_%u;\n", ir->TargetBB->base.id);
	    break;
	}
	//case OPCODE_IJumpIf: {
	//    IJumpIf *ir = (IJumpIf *)Inst;
	//    assert(0 && "need test");
	//    cgen_printf(gen, "if (RTEST(v%d)) {\n"
	//                     "    goto L_%u;\n"
	//                     "}\n",
	//                lir_getid(ir->Cond), ir->TargetBB->base.id);
	//    break;
	//}
	//case OPCODE_IThrow: {
	//    // IThrow *ir = (IThrow *) Inst;
	//    assert(0 && "not implemented");
	//    break;
	//}
	// case OPCODE_IFramePush: {
	//     IFramePush *ir = (IFramePush *)Inst;
	//     EmitFramePush(Rec, gen, ir, 0);
	//     break;
	// }
	case OPCODE_IFramePop: {
	    // IFramePop *ir = (IFramePop *) Inst;
	    cgen_printf(gen, "jit_vm_pop_frame(th);\n"
	                     "reg_cfp = th->cfp;\n");
	    break;
	}
	case OPCODE_IReturn: {
	    cgen_printf(gen, "jit_vm_pop_frame(th);\n"
	                     "reg_cfp = th->cfp;\n"
	                     "(void)v%d;\n",
	                Id);
	    break;
	}
	case OPCODE_IPhi: {
	    // IPhi *ir = (IPhi *) Inst;
	    assert(0 && "not implemented");
	    break;
	}
	case OPCODE_ITrace: {
	    ITrace *ir = (ITrace *)Inst;
	    if (0) {
		// FIXME
		// When we enable trace code, clang saied error.
		// >> error: Must have a valid dtrace stability entry'
		// >> ld: error creating dtrace DOF section for architecture x86_64'
		// Need to enable dtrace for compatibility but we have no time to
		// implement.
		cgen_printf(gen,
		            "{\n"
		            "  rb_event_flag_t flag = (rb_event_flag_t)%u;\n"
		            "  if (RUBY_DTRACE_METHOD_ENTRY_ENABLED() ||\n"
		            "      RUBY_DTRACE_METHOD_RETURN_ENABLED() ||\n"
		            "      RUBY_DTRACE_CMETHOD_ENTRY_ENABLED() ||\n"
		            "      RUBY_DTRACE_CMETHOD_RETURN_ENABLED()) {\n"
		            "\n"
		            "    switch(flag) {\n"
		            "      case RUBY_EVENT_CALL:\n"
		            "        RUBY_DTRACE_METHOD_ENTRY_HOOK(th, 0, 0);\n"
		            "        break;\n"
		            "      case RUBY_EVENT_C_CALL:\n"
		            "        RUBY_DTRACE_CMETHOD_ENTRY_HOOK(th, 0, 0);\n"
		            "        break;\n"
		            "      case RUBY_EVENT_RETURN:\n"
		            "        RUBY_DTRACE_METHOD_RETURN_HOOK(th, 0, 0);\n"
		            "        break;\n"
		            "      case RUBY_EVENT_C_RETURN:\n"
		            "        RUBY_DTRACE_CMETHOD_RETURN_HOOK(th, 0, 0);\n"
		            "        break;\n"
		            "    }\n"
		            "  }\n"
		            "\n"
		            "  EXEC_EVENT_HOOK(th, flag, GET_SELF(), 0,\n"
		            "      0/*id and klass are resolved at callee */,\n"
		            "      (flag & (RUBY_EVENT_RETURN |\n"
		            "      RUBY_EVENT_B_RETURN)) ? TOPN(0) : Qundef);\n"
		            "}\n",
		            ir->flag);
	    }
	    break;
	}
	default:
	    assert(0 && "unreachable");
    }
}

static void prepare_side_exit(CGen *gen, lir_builder_t *builder, lir_func_t *func)
{
    unsigned i, j, k;
    jit_list_t *side_exits = &func->side_exits;
    jit_trace_t *trace = builder->cur_trace;
    for (i = 0; i < jit_list_size(side_exits); i++) {
	jit_snapshot_t *snapshot = JIT_LIST_GET(jit_snapshot_t *, side_exits, i);
	VALUE *pc = snapshot->pc;
	trace_side_exit_handler_t *hdl;
	if (LIR_OPT_REMOVE_DEAD_SIDE_EXIT && snapshot->refc == 0) {
	    continue;
	}
	hdl = (trace_side_exit_handler_t *)malloc(sizeof(*hdl));
	hdl->exit_pc = pc;
	hdl->this_trace = trace;
	hdl->child_trace = NULL;
	hdl->exit_status = snapshot->status;
	JIT_LIST_ADD(&trace->exit_handlers, hdl);
	assert(i < MAX_SIDE_EXIT);
    }

    for (i = 0; i < jit_list_size(&func->bblist); i++) {
	basicblock_t *bb = JIT_LIST_GET(basicblock_t *, &func->bblist, i);
	lir_builder_set_bb(builder, bb);
	for (j = 0; j < jit_list_size(&bb->insts); j++) {
	    lir_t inst = JIT_LIST_GET(lir_t, &bb->insts, j);
	    if (lir_is_guard(inst) || inst->opcode == OPCODE_IExit) {
		VALUE *pc = ((IExit *)inst)->Exit;
		jit_snapshot_t *snapshot = NULL;
		for (k = 0; k < jit_list_size(side_exits); k++) {
		    snapshot = JIT_LIST_GET(jit_snapshot_t *, side_exits, k);
		    if (snapshot->pc == pc) {
			break;
		    }
		    snapshot = NULL;
		}
		assert(snapshot != NULL);
		((IExit *)inst)->Exit = (VALUE *)(uintptr_t) k;
	    }
	}
    }
}

static void compile_prologue(CGen *gen, lir_builder_t *builder, lir_func_t *func)
{
    unsigned i;
    cgen_printf(gen,
                "#include \"ruby_jit.h\"\n"
                "#include <assert.h>\n"
                "#include <dlfcn.h>\n"
                "#define BLOCK_LABEL(label) L_##label:;(void)&&L_##label;\n"
                "static const jit_runtime_t *local_jit_runtime = NULL;\n"
                "static trace_side_exit_handler_t **exit_handlers = NULL;\n");
#if 0
    if (JIT_DUMP_COMPILE_LOG > 0) {
	const rb_iseq_t *iseq = trace->iseq;
	VALUE file = iseq->location.path;
	cgen_printf(gen,
	            "// This code is translated from file=%s line=%d\n",
	            RSTRING_PTR(file),
	            rb_iseq_line_no(iseq,
	                            trace->start_pc - iseq->iseq_encoded));
    }
#endif

    prepare_side_exit(gen, builder, func);

    cgen_printf(gen,
                "void init_ruby_jit_%d(const jit_runtime_t *ctx, struct jit_trace_t *t, trace_side_exit_handler_t **hdls)"
                "{\n"
                "  local_jit_runtime = ctx;\n"
                "  exit_handlers = hdls;\n"
                "  (void) make_no_method_exception;\n"
                "}\n",
                func->id);
    cgen_printf(gen, "trace_side_exit_handler_t *ruby_jit_%d(rb_thread_t *th,\n"
                     "    rb_control_frame_t *reg_cfp)\n"
                     "{\n",
                func->id);
}

static void compile_epilogue(CGen *gen, lir_builder_t *builder)
{
    cgen_printf(gen, "}\n"); // end of ruby_jit_%d
}

static void assert_not_undef(CGen *gen, lir_t inst)
{
    cgen_printf(gen, "assert(v%d != Qundef);\n", lir_getid(inst));
}

static void compile_sideexit(CGen *gen, lir_builder_t *builder, lir_func_t *func)
{
    /*
      sp[0..n] = StackMap[0..n]
    * return PC;
    */
    unsigned i, j;
    for (j = 0; j < jit_list_size(&func->side_exits); j++) {
	jit_snapshot_t *snapshot;
	snapshot = JIT_LIST_GET(jit_snapshot_t *, &func->side_exits, j);
	if (LIR_OPT_REMOVE_DEAD_SIDE_EXIT && snapshot->refc == 0) {
	    continue;
	}
	cgen_printf(gen, "L_exit%u:; // pc=%p\n", j, snapshot->pc);
	if (JIT_DEBUG_VERBOSE >= 10) {
	    cgen_printf(gen, "__int3__;\n");
	}
	if (JIT_DEBUG_VERBOSE > 1) {
	    cgen_printf(gen, "fprintf(stderr,\"%%s %%04d exit%u : pc=%p\\n\", __FILE__, __LINE__);\n", j, snapshot->pc);
	    for (i = 0; i < jit_list_size(&snapshot->insts); i++) {
		lir_t inst = JIT_LIST_GET(lir_t, &snapshot->insts, i);
		if (inst && lir_opcode(inst) == OPCODE_IStackPush) {
		    IStackPush *sp = (IStackPush *)inst;
		    assert_not_undef(gen, sp->Val);
		}
	    }
	}
	cgen_printf(gen, "return exit_handlers[%u];\n", j);
    }
}

static void cgen_compile2(CGen *gen, lir_builder_t *builder, lir_func_t *func)
{
    unsigned i, j;
    compile_prologue(gen, builder, func);
    for (i = 0; i < jit_list_size(&func->bblist); i++) {
	basicblock_t *bb = JIT_LIST_GET(basicblock_t *, &func->bblist, i);
	for (j = 0; j < bb->insts.size; j++) {
	    lir_t inst = JIT_LIST_GET(lir_t, &bb->insts, j);
	    if (lir_inst_define_value(lir_opcode(inst))) {
		int Id = lir_getid(inst);
		cgen_printf(gen, "VALUE v%d = Qundef;\n", Id);
	    }
	}
    }

    for (i = 0; i < jit_list_size(&func->bblist); i++) {
	basicblock_t *bb = JIT_LIST_GET(basicblock_t *, &func->bblist, i);
	cgen_printf(gen, "BLOCK_LABEL(%u);\n", bb->base.id);
	for (j = 0; j < bb->insts.size; j++) {
	    lir_t inst = JIT_LIST_GET(lir_t, &bb->insts, j);
	    compile_inst(gen, inst);
	}
    }

    compile_sideexit(gen, builder, func);
    compile_epilogue(gen, builder);
}

static rujit_backend_t backend_cgen = {
    NULL,
    cgen_init,
    cgen_delete,
    cgen_compile,
    cgen_unload
};

static void jit_backend_init_cgen(rujit_t *jit)
{
    jit->backend = &backend_cgen;
}
