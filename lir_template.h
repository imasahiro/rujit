#ifndef LIR_TEMPLATE_H
#define LIR_TEMPLATE_H

static inline VALUE rb_jit_exec_IFixnumAddOverflow(VALUE recv, VALUE obj)
{
    VALUE val;
#ifndef LONG_LONG_VALUE
    val = (recv + (obj & (~1)));
    if ((~(recv ^ obj) & (recv ^ val)) & ((VALUE)0x01 << ((sizeof(VALUE) * CHAR_BIT) - 1))) {
	val = rb_big_plus(rb_int2big(FIX2LONG(recv)),
	                  rb_int2big(FIX2LONG(obj)));
    }
#else
    long a, b, c;
    a = FIX2LONG(recv);
    b = FIX2LONG(obj);
    c = a + b;
    if (FIXABLE(c)) {
	val = LONG2FIX(c);
    }
    else {
	val = rb_big_plus(rb_int2big(a), rb_int2big(b));
    }
#endif
    return val;
}
static inline VALUE rb_jit_exec_IFixnumSubOverflow(VALUE recv, VALUE obj)
{
    VALUE val;
    long a, b, c;

    a = FIX2LONG(recv);
    b = FIX2LONG(obj);
    c = a - b;

    if (FIXABLE(c)) {
	val = LONG2FIX(c);
    }
    else {
	val = rb_big_minus(rb_int2big(a), rb_int2big(b));
    }
    return val;
}
static inline VALUE rb_jit_exec_IFixnumMulOverflow(VALUE recv, VALUE obj)
{
    VALUE val;
    long a, b;

    a = FIX2LONG(recv);
    if (a == 0) {
	val = recv;
    }
    else {
	b = FIX2LONG(obj);
	if (MUL_OVERFLOW_FIXNUM_P(a, b)) {
	    val = rb_big_mul(rb_int2big(a), rb_int2big(b));
	}
	else {
	    val = LONG2FIX(a * b);
	}
    }
    return val;
}
static inline VALUE rb_jit_exec_IFixnumDivOverflow(VALUE recv, VALUE obj)
{
    long x = FIX2LONG(recv);
    long y = FIX2LONG(obj);
    long div, mod;
    x = (x > 0) ? x : -x;
    y = (y > 0) ? y : -y;
    div = x / y;
    mod = x - div * y;

    if ((mod < 0 && y > 0) || (mod > 0 && y < 0)) {
	mod += y;
	div -= 1;
    }
    return LONG2NUM(div);
}
static inline VALUE rb_jit_exec_IFixnumModOverflow(VALUE recv, VALUE obj)
{
    long x = FIX2LONG(recv);
    long y = FIX2LONG(obj);
    long div, mod;
    x = (x > 0) ? x : -x;
    y = (y > 0) ? y : -y;
    div = x / y;
    mod = x - div * y;
    if ((mod < 0 && y > 0) || (mod > 0 && y < 0)) {
	mod += y;
	div -= 1;
    }
    return LONG2NUM(mod);
}

static inline VALUE rb_jit_exec_IFixnumAdd(VALUE recv, VALUE obj)
{
    VALUE val = (recv + (obj & (~1))) | FIXNUM_FLAG;
    return val;
}
static inline VALUE rb_jit_exec_IFixnumSub(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) - FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumMul(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) * FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumDiv(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) / FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumMod(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) % FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumAnd(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) & FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumOr(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) | FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumXor(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) ^ FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumLshift(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) << FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumRshift(VALUE arg0, VALUE arg1)
{
    return LONG2FIX(FIX2LONG(arg0) >> FIX2LONG(arg1));
}
static inline VALUE rb_jit_exec_IFixnumComplement(VALUE arg0)
{
    return ~arg0 | FIXNUM_FLAG;
}
static inline VALUE rb_jit_exec_IFixnumNeg(VALUE arg0)
{
    long x = FIX2LONG(arg0);
    return (-x) | FIXNUM_FLAG;
}
static inline VALUE rb_jit_exec_IFixnumEq(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv == obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumNe(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv != obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumGt(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv > obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumGe(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv >= obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumLt(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv < obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumLe(VALUE arg0, VALUE arg1)
{
    SIGNED_VALUE recv = arg0;
    SIGNED_VALUE obj = arg1;
    return (recv <= obj) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatAdd(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(RFLOAT_VALUE(arg0) + RFLOAT_VALUE(arg1));
}
static inline VALUE rb_jit_exec_IFloatSub(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(RFLOAT_VALUE(arg0) - RFLOAT_VALUE(arg1));
}
static inline VALUE rb_jit_exec_IFloatMul(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(RFLOAT_VALUE(arg0) * RFLOAT_VALUE(arg1));
}
static inline VALUE rb_jit_exec_IFloatDiv(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(RFLOAT_VALUE(arg0) / RFLOAT_VALUE(arg1));
}
static inline VALUE rb_jit_exec_IFloatMod(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(ruby_float_mod(RFLOAT_VALUE(arg0), RFLOAT_VALUE(arg1)));
}
static inline VALUE rb_jit_exec_IFloatPow(VALUE arg0, VALUE arg1)
{
    return DBL2NUM(pow(RFLOAT_VALUE(arg0), (double)FIX2LONG(arg1)));
}
static inline VALUE rb_jit_exec_IFloatNeg(VALUE arg0)
{
    return DBL2NUM(-RFLOAT_VALUE(arg0));
}
static inline VALUE rb_jit_exec_IFloatEq(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) == RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatNe(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) != RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatGt(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) > RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatGe(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) >= RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatLt(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) < RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFloatLe(VALUE arg0, VALUE arg1)
{
    return (RFLOAT_VALUE(arg0) <= RFLOAT_VALUE(arg1)) ? Qtrue : Qfalse;
}
static inline VALUE rb_jit_exec_IFixnumToFloat(VALUE arg0)
{
    return DBL2NUM((double)FIX2LONG(arg0));
}
static inline VALUE rb_jit_exec_IFixnumToString(VALUE arg0)
{
    return rb_fix2str(arg0, 10);
}
static inline VALUE rb_jit_exec_IFloatToFixnum(VALUE arg0)
{
    return LONG2FIX((long)RFLOAT_VALUE(arg0));
}
//static inline VALUE rb_jit_exec_IFloatToString(VALUE arg0)
//{
//    return flo_to_s(arg0);
//}
static inline VALUE rb_jit_exec_IObjectToString(VALUE arg0)
{
    return rb_obj_as_string(arg0);
}
static inline VALUE rb_jit_exec_IStringToFixnum(VALUE arg0)
{
    return rb_str_to_inum(arg0, 10, 0);
}
static inline VALUE rb_jit_exec_IStringToFloat(VALUE arg0)
{
    return DBL2NUM(rb_str_to_dbl(arg0, 0));
}
static inline VALUE rb_jit_exec_IStringAdd(VALUE arg0, VALUE arg1)
{
    return rb_str_append(arg0, arg1);
}
#endif /* end of include guard */
