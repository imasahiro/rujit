/**********************************************************************

  jit_profile.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include <sys/time.h> // gettimeofday

#define JIT_PROFILE_ENTER(msg) jit_profile((msg), 0)
#define JIT_PROFILE_LEAVE(msg, cond) jit_profile((msg), (cond))
#ifdef ENABLE_PROFILE_TRACE_JIT
static uint64_t time_trace_search = 0;
static uint64_t invoke_trace_invoke_enter = 0;
static uint64_t invoke_trace_invoke = 0;
static uint64_t invoke_trace_child1 = 0;
static uint64_t invoke_trace_child2 = 0;
static uint64_t invoke_trace_exit = 0;
static uint64_t invoke_trace_side_exit = 0;
static uint64_t invoke_trace_success = 0;
static uint64_t invoke_bloomfilter_hit = 0;
static uint64_t invoke_bloomfilter_total = 0;
static uint64_t invoke_bloomfilter_entry = 0;
#define JIT_PROFILE_COUNT(COUNTER) ((COUNTER) += 1)
#else
#define JIT_PROFILE_COUNT(COUNTER)
#endif

static uint64_t jit_profile(const char *msg, int print_log)
{
    static uint64_t last = 0;
    uint64_t time, diff;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    diff = time - last;
    if (print_log) {
	fprintf(stderr, "%s : %u msec\n", msg, (unsigned)diff);
    }
    last = time;
    return diff;
}

static void jit_profile_dump()
{
#ifdef ENABLE_PROFILE_TRACE_JIT
#define DUMP_COUNT(COUNTER) \
    fprintf(stderr, #COUNTER "  %" PRIu64 "\n", COUNTER)
    DUMP_COUNT(invoke_trace_invoke_enter);
    DUMP_COUNT(invoke_trace_invoke);
    DUMP_COUNT(invoke_trace_child1);
    DUMP_COUNT(invoke_trace_child2);
    DUMP_COUNT(invoke_trace_exit);
    DUMP_COUNT(invoke_trace_side_exit);
    DUMP_COUNT(invoke_trace_success);
    DUMP_COUNT(invoke_bloomfilter_entry);
    DUMP_COUNT(invoke_bloomfilter_hit);
    DUMP_COUNT(invoke_bloomfilter_total);
    DUMP_COUNT(time_trace_search);
#undef DUMP_COUNT
#endif
}
