/**********************************************************************

  ruby_jit.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#include "ruby/ruby.h"
#include "ruby/vm.h"
#include "ruby/st.h"
#include "method.h"
#include "vm_core.h"
#include "vm_exec.h"
#include "vm_insnhelper.h"
#include "internal.h"
#include "iseq.h"

#include "probes.h"
#include "probes_helper.h"
#include "jit.h"

#ifndef JIT_RUBY_JIT_H
#define JIT_RUBY_JIT_H

#define JIT_OP_UNREDEFINED_P(op, klass) (LIKELY((JIT_RUNTIME->redefined_flag[(op)] & (klass)) == 0))
#define JIT_GET_GLOBAL_METHOD_STATE() (*(JIT_RUNTIME)->global_method_state)
#define JIT_GET_GLOBAL_CONSTANT_STATE() (*(JIT_RUNTIME)->global_constant_state)

#ifndef JIT_HOST
typedef VALUE (*jit_native_func0_t)();
typedef VALUE (*jit_native_func1_t)(VALUE);
typedef VALUE (*jit_native_func2_t)(VALUE, VALUE);
typedef VALUE (*jit_native_func3_t)(VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func4_t)(VALUE, VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func5_t)(VALUE, VALUE, VALUE, VALUE, VALUE);
typedef VALUE (*jit_native_func6_t)(VALUE, VALUE, VALUE, VALUE, VALUE, VALUE);

void rb_out_of_int(SIGNED_VALUE num) { JIT_RUNTIME->_rb_out_of_int(num); }

VALUE rb_int2big(SIGNED_VALUE v) { return JIT_RUNTIME->_rb_int2big(v); }

VALUE rb_float_new_in_heap(double d)
{
    return JIT_RUNTIME->_rb_float_new_in_heap(d);
}

static inline VALUE make_no_method_exception(VALUE exc, const char *format,
                                             VALUE obj, int argc,
                                             const VALUE *argv)
{
    return JIT_RUNTIME->_make_no_method_exception(exc, format, obj, argc, argv);
}

#if USE_RGENGC == 1
int rb_gc_writebarrier_incremental(VALUE a, VALUE b)
{
    return JIT_RUNTIME->_rb_gc_writebarrier_incremental(a, b);
}

void rb_gc_writebarrier_generational(VALUE a, VALUE b)
{
    return JIT_RUNTIME->_rb_gc_writebarrier_generational(a, b);
}
#endif
#define rb_cArray (JIT_RUNTIME->cArray)
#define rb_cFixnum (JIT_RUNTIME->cFixnum)
#define rb_cFloat (JIT_RUNTIME->cFloat)
#define rb_cHash (JIT_RUNTIME->cHash)
#define rb_cRegexp (JIT_RUNTIME->cRegexp)
#define rb_cString (JIT_RUNTIME->cString)
#define rb_cTime (JIT_RUNTIME->cTime)
#define rb_cSymbol (JIT_RUNTIME->cSymbol)
#define rb_cProc (JIT_RUNTIME->cProc)

#define rb_cTrueClass (JIT_RUNTIME->cTrueClass)
#define rb_cFalseClass (JIT_RUNTIME->cFalseClass)
#define rb_cNilClass (JIT_RUNTIME->cNilClass)

#define rb_check_array_type (JIT_RUNTIME->_rb_check_array_type)
#define rb_big_plus (JIT_RUNTIME->_rb_big_plus)
#define rb_big_minus (JIT_RUNTIME->_rb_big_minus)
#define rb_big_mul (JIT_RUNTIME->_rb_big_mul)
#define rb_int2big (JIT_RUNTIME->_rb_int2big)
#define rb_str_plus (JIT_RUNTIME->_rb_str_plus)
#define rb_str_append (JIT_RUNTIME->_rb_str_append)
#define rb_str_length (JIT_RUNTIME->_rb_str_length)
#define rb_str_resurrect (JIT_RUNTIME->_rb_str_resurrect)
#define rb_range_new (JIT_RUNTIME->_rb_range_new)
#define rb_hash_new (JIT_RUNTIME->_rb_hash_new)
#define rb_hash_aref (JIT_RUNTIME->_rb_hash_aref)
#define rb_hash_aset (JIT_RUNTIME->_rb_hash_aset)
#define rb_reg_match (JIT_RUNTIME->_rb_reg_match)
#define rb_reg_new_ary (JIT_RUNTIME->_rb_reg_new_ary)
#define rb_ary_new (JIT_RUNTIME->_rb_ary_new)
#define rb_ary_new_from_values (JIT_RUNTIME->_rb_ary_new_from_values)
#define rb_ary_push (JIT_RUNTIME->_rb_ary_push)
#define rb_gvar_get (JIT_RUNTIME->_rb_gvar_get)
#define rb_gvar_set (JIT_RUNTIME->_rb_gvar_set)
#define rb_obj_alloc (JIT_RUNTIME->_rb_obj_alloc)
#define rb_obj_as_string (JIT_RUNTIME->_rb_obj_as_string)
#define rb_ivar_set JIT_RUNTIME->_rb_ivar_set

#define make_no_method_exception JIT_RUNTIME->_make_no_method_exception
#define rb_ary_entry JIT_RUNTIME->_rb_ary_entry
#define rb_ary_plus JIT_RUNTIME->_rb_ary_plus
#define rb_ary_store JIT_RUNTIME->_rb_ary_store
#define rb_ary_resurrect JIT_RUNTIME->_rb_ary_resurrect
#define ruby_float_mod JIT_RUNTIME->_ruby_float_mod
#define rb_float_new_in_heap JIT_RUNTIME->_rb_float_new_in_heap
#define jit_vm_redefined_flag JIT_RUNTIME->_jit_vm_redefined_flag
#if HAVE_RB_GC_GUARDED_PTR_VAL
#define rb_gc_guarded_ptr_val JIT_RUNTIME->_rb_gc_guarded_ptr_val
#endif

#define rb_vm_make_proc JIT_RUNTIME->_rb_vm_make_proc
#define rb_gc_writebarrier JIT_RUNTIME->_rb_gc_writebarrier
#define rb_exc_raise JIT_RUNTIME->_rb_exc_raise
#define rb_out_of_int JIT_RUNTIME->_rb_out_of_int
#define ruby_current_vm JIT_RUNTIME->_ruby_current_vm

#undef CLASS_OF
#define CLASS_OF(O) jit_rb_class_of(O)

static inline VALUE jit_rb_class_of(VALUE obj)
{
    if (IMMEDIATE_P(obj)) {
	if (FIXNUM_P(obj))
	    return rb_cFixnum;
	if (FLONUM_P(obj))
	    return rb_cFloat;
	if (obj == Qtrue)
	    return rb_cTrueClass;
	if (STATIC_SYM_P(obj))
	    return rb_cSymbol;
    }
    else if (!RTEST(obj)) {
	if (obj == Qnil)
	    return rb_cNilClass;
	if (obj == Qfalse)
	    return rb_cFalseClass;
    }
    return RBASIC(obj)->klass;
}

static inline void vm_stackoverflow(void)
{
    rb_exc_raise(sysstack_error);
}

static inline VALUE *
VM_EP_LEP(VALUE *ep)
{
    while (!VM_EP_LEP_P(ep)) {
	ep = VM_EP_PREV_EP(ep);
    }
    return ep;
}

static inline VALUE *
JIT_CF_LEP(rb_control_frame_t *cfp)
{
    return VM_EP_LEP(cfp->ep);
}

static inline VALUE *
JIT_CF_PREV_EP(rb_control_frame_t *cfp)
{
    return VM_EP_PREV_EP((cfp)->ep);
}

static inline rb_block_t *
JIT_CF_BLOCK_PTR(rb_control_frame_t *cfp)
{
    VALUE *ep = JIT_CF_LEP(cfp);
    return VM_EP_BLOCK_PTR(ep);
}

static inline VALUE
jit_vm_call_iseq_setup_normal(rb_thread_t *th, rb_control_frame_t *cfp, rb_call_info_t *ci)
{
    int i, local_size;
    VALUE *argv = cfp->sp - ci->argc;
    rb_iseq_t *iseq = ci->me->def->body.iseq;
    VALUE *sp = argv + iseq->param.size;

    /* clear local variables (arg_size...local_size) */
    for (i = iseq->param.size, local_size = iseq->local_size; i < local_size; i++) {
	*sp++ = Qnil;
    }

    jit_vm_push_frame(th, iseq, VM_FRAME_MAGIC_METHOD, ci->recv, ci->defined_class,
                      VM_ENVVAL_BLOCK_PTR(ci->blockptr),
                      iseq->iseq_encoded + ci->aux.opt_pc, sp, 0, ci->me, iseq->stack_max);

    cfp->sp = argv - 1 /* recv */;
    return Qundef;
}

static inline VALUE
jit_vm_call_block_setup(rb_thread_t *th, rb_control_frame_t *reg_cfp, rb_block_t *block, rb_call_info_t *ci, int argc)
{
    int opt_pc = 0;
    rb_iseq_t *iseq = block->iseq;
    const int arg_size = iseq->param.size;
    int is_lambda = jit_block_proc_is_lambda(block->proc);
    VALUE *const rsp = GET_SP() - ci->argc;

    SET_SP(rsp);

    jit_vm_push_frame(th, iseq,
                      is_lambda ? VM_FRAME_MAGIC_LAMBDA : VM_FRAME_MAGIC_BLOCK,
                      block->self,
                      block->klass,
                      VM_ENVVAL_PREV_EP_PTR(block->ep),
                      iseq->iseq_encoded + opt_pc,
                      rsp + arg_size,
                      iseq->local_size - arg_size, 0, iseq->stack_max);

    return Qundef;
}

#endif
#include "lir_template.h"
#define __int3__ __asm__ __volatile__("int3");
#endif /* end of include guard */
