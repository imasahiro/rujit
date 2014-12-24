/**********************************************************************

  jit.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#ifndef RUBY_JIT_H
#define RUBY_JIT_H 1

// extern int rujit_trace(rb_thread_t *, rb_control_frame_t *reg_cfp, VALUE *reg_pc, int opcode);
extern void rujit_check_redefinition_opt_method(const rb_method_entry_t *me, VALUE klass);
extern void rujit_record_insn(rb_thread_t *, rb_control_frame_t *reg_cfp, VALUE *reg_pc);
extern int rujit_invoke_or_make_trace(rb_thread_t *, rb_control_frame_t *reg_cfp, VALUE *reg_pc);
extern void rujit_notify_proc_freed(void *ptr);

extern void Destruct_rawjit();

extern int rujit_record_trace_mode;

struct rb_vm_global_state {
    rb_serial_t *_global_method_state;
    rb_serial_t *_global_constant_state;
    rb_serial_t *_class_serial;
    short *_ruby_vm_redefined_flag;
};

typedef struct jit_runtime_struct {
    VALUE cArray;
    VALUE cFixnum;
    VALUE cFloat;
    VALUE cHash;
    VALUE cMath;
    VALUE cRegexp;
    VALUE cString;
    VALUE cTime;
    VALUE cSymbol;
    VALUE cProc;

    VALUE cFalseClass;
    VALUE cTrueClass;
    VALUE cNilClass;

    // global data
    short *redefined_flag;
    rb_serial_t *global_method_state;
    rb_serial_t *global_constant_state;
    rb_serial_t *class_serial;
    struct rb_vm_struct *_ruby_current_vm;

    // ruby API
    // type check API
    VALUE (*_rb_check_array_type)(VALUE);
    VALUE (*_rb_big_plus)(VALUE, VALUE);
    VALUE (*_rb_big_minus)(VALUE, VALUE);
    VALUE (*_rb_big_mul)(VALUE, VALUE);
    VALUE (*_rb_int2big)(SIGNED_VALUE);
    VALUE (*_rb_str_length)(VALUE);
    VALUE (*_rb_str_plus)(VALUE, VALUE);
    VALUE (*_rb_str_append)(VALUE, VALUE);
    VALUE (*_rb_str_resurrect)(VALUE);
    VALUE (*_rb_range_new)(VALUE, VALUE, int);
    VALUE (*_rb_hash_new)();
    VALUE (*_rb_hash_aref)(VALUE, VALUE);
    VALUE (*_rb_hash_aset)(VALUE, VALUE, VALUE);
    VALUE (*_rb_reg_match)(VALUE, VALUE);
    VALUE (*_rb_reg_new_ary)(VALUE, int);
    VALUE (*_rb_ary_new)();
    VALUE (*_rb_ary_new_from_values)(long n, const VALUE *elts);
    VALUE (*_rb_ary_plus)(VALUE x, VALUE y);
    VALUE (*_rb_ary_push)(VALUE x, VALUE y);
    VALUE (*_rb_gvar_get)(struct rb_global_entry *);
    VALUE (*_rb_gvar_set)(struct rb_global_entry *, VALUE);
    VALUE (*_rb_obj_alloc)(VALUE klass);
    VALUE (*_rb_obj_as_string)(VALUE);
    VALUE (*_rb_ivar_set)(VALUE obj, ID id, VALUE val);

    // Internal ruby APIs
    double (*_ruby_float_mod)(double, double);
    VALUE (*_rb_float_new_in_heap)(double);
    VALUE (*_rb_ary_entry)(VALUE, long);
    void (*_rb_ary_store)(VALUE, long, VALUE);
    VALUE (*_rb_ary_resurrect)(VALUE ary);
#if HAVE_RB_GC_GUARDED_PTR_VAL
    volatile VALUE *(*_rb_gc_guarded_ptr_val)(volatile VALUE *ptr, VALUE val);
#endif
#if USE_RGENGC == 1
    int (*_rb_gc_writebarrier_incremental)(VALUE, VALUE);
#endif
    void (*_rb_gc_writebarrier_generational)(VALUE, VALUE);
#if SIZEOF_INT < SIZEOF_VALUE
    NORETURN(void (*_rb_out_of_int)(SIGNED_VALUE num));
#endif
    NORETURN(void (*_rb_exc_raise)(VALUE));
    VALUE (*_make_no_method_exception)(VALUE exc, const char *format, VALUE obj,
                                       int argc, const VALUE *argv);
    VALUE (*_check_match)(VALUE, VALUE, enum vm_check_match_type);
    VALUE (*_rb_vm_make_proc)(rb_thread_t *th, const rb_block_t *block, VALUE klass);
} jit_runtime_t;

extern jit_runtime_t jit_runtime;

enum JIT_BOP {
    JIT_BOP_PLUS = BOP_PLUS,
    JIT_BOP_MINUS = BOP_MINUS,
    JIT_BOP_MULT = BOP_MULT,
    JIT_BOP_DIV = BOP_DIV,
    JIT_BOP_MOD = BOP_MOD,
    JIT_BOP_EQ = BOP_EQ,
    JIT_BOP_EQQ = BOP_EQQ,
    JIT_BOP_LT = BOP_LT,
    JIT_BOP_LE = BOP_LE,
    JIT_BOP_LTLT = BOP_LTLT,
    JIT_BOP_AREF = BOP_AREF,
    JIT_BOP_ASET = BOP_ASET,
    JIT_BOP_LENGTH = BOP_LENGTH,
    JIT_BOP_SIZE = BOP_SIZE,
    JIT_BOP_EMPTY_P = BOP_EMPTY_P,
    JIT_BOP_SUCC = BOP_SUCC,
    JIT_BOP_GT = BOP_GT,
    JIT_BOP_GE = BOP_GE,
    JIT_BOP_NOT = BOP_NOT,
    JIT_BOP_NEQ = BOP_NEQ,
    JIT_BOP_MATCH = BOP_MATCH,
    JIT_BOP_FREEZE = BOP_FREEZE,

    JIT_BOP_LAST_ = BOP_LAST_,
    JIT_BOP_AND,
    JIT_BOP_OR,
    JIT_BOP_XOR,
    JIT_BOP_INV,
    JIT_BOP_RSHIFT,
    JIT_BOP_POW,
    JIT_BOP_NEG,

    JIT_BOP_TO_F,
    JIT_BOP_TO_I,
    JIT_BOP_TO_S,

    JIT_BOP_SIN,
    JIT_BOP_COS,
    JIT_BOP_TAN,
    JIT_BOP_LOG2,
    JIT_BOP_LOG10,
    JIT_BOP_EXP,
    JIT_BOP_SQRT,
    JIT_BOP_EXT_LAST_
};

typedef enum trace_exit_staus {
    TRACE_EXIT_ERROR = -1,
    TRACE_EXIT_SUCCESS = 0,
    TRACE_EXIT_SIDE_EXIT
} trace_exit_status_t;

typedef struct trace_side_exit_handler {
    struct jit_trace *this_trace;
    struct jit_trace *child_trace;
    VALUE *exit_pc;
    trace_exit_status_t exit_status;
} trace_side_exit_handler_t;

#ifdef JIT_HOST
#define JIT_RUNTIME (&jit_runtime)
#else
static const jit_runtime_t *local_jit_runtime;
#define JIT_RUNTIME (local_jit_runtime)
#endif

#endif /* end of include guard */
