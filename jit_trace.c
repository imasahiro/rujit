
#define TRACE_ERROR_INFO(OP, TAIL)                                        \
    OP(OK, "ok")                                                          \
    OP(NATIVE_METHOD, "invoking native method")                           \
    OP(THROW, "throw exception")                                          \
    OP(UNSUPPORT_OP, "not supported bytecode")                            \
    OP(LEAVE, "this trace return into native method")                     \
    OP(REGSTACK_UNDERFLOW, "register stack underflow")                    \
    OP(ALREADY_RECORDED, "this instruction is already recorded on trace") \
    OP(BUFFER_FULL, "trace buffer is full")                               \
    TAIL

#define DEFINE_TRACE_ERROR_STATE(NAME, MSG) TRACE_ERROR_##NAME,
#define DEFINE_TRACE_ERROR_MESSAGE(NAME, MSG) MSG,
enum trace_error_state {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_STATE, TRACE_ERROR_END = -1)
};

static const char *trace_error_message[] = {
    TRACE_ERROR_INFO(DEFINE_TRACE_ERROR_MESSAGE, "")
};

typedef struct trace_side_exit_handler trace_side_exit_handler_t;

typedef struct jit_trace_t {
    void *code;
    VALUE *start_pc;
    VALUE *last_pc;
    trace_side_exit_handler_t *parent;
    long profile_counter;
    long failed_counter;
#if ENABLE_PROFILE_TRACE_JIT
    long invoke_counter;
#endif
    long refc;
    hashmap_t *side_exit;
    const_pool_t cpool;
#if JIT_DEBUG_TRACE
    char *func_name;
#endif
    const ISEQ iseq; // debug suage
} jit_trace_t;

typedef enum trace_exit_staus {
    TRACE_EXIT_ERROR = -1,
    TRACE_EXIT_SUCCESS = 0,
    TRACE_EXIT_SIDE_EXIT
} trace_exit_status_t;

struct trace_side_exit_handler {
    struct jit_trace *this_trace;
    struct jit_trace *child_trace;
    VALUE *exit_pc;
    trace_exit_status_t exit_status;
};
