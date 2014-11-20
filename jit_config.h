/**********************************************************************

  jit_config.h -

  $Author$

  Copyright (C) 2014 Masahiro Ide

 **********************************************************************/

#ifndef JIT_CONFIG_H
#define JIT_CONFIG_H

// When `USE_CGEN` is defined,  we use c-generator as backend of jit compiler.
// If `USE_LLVM` is defined, use llvm as backend.
#define USE_CGEN 1
//#define USE_LLVM 1

#define DUMP_STACK_MAP 0 /* 0:disable, 1:dump, 2:verbose */
//#define DUMP_LLVM_IR   0 /* 0:disable, 1:dump, 2:dump non-optimized llvm ir */
#define DUMP_INST 0 /* 0:disable, 1:dump */
#define DUMP_LIR 1 /* 0:disable, 1:dump */
// #define DUMP_LIR_DUMP_VALUE_AS_STRING 1
#define DUMP_CALL_STACK_MAP 0 /* 0:disable, 1:dump */

#define JIT_DUMP_COMPILE_LOG 1 /* 0:disable, 1:dump, 2:verbose */
#define LIR_MIN_TRACE_LENGTH 8 /* min length of instructions jit compile */
#define LIR_MAX_TRACE_LENGTH 1024 /* max length of instructions jit compile */
#define LIR_TRACE_INIT_SIZE 16 /* initial size of trace */
#define LIR_RESERVED_REGSTACK_SIZE 8

#define JIT_DEBUG_VERBOSE 10 /* 0:disable, 1: emit log, 2: verbose, 10: */
#define JIT_DEBUG_TRACE 1
#define JIT_LOG_SIDE_EXIT 1 /* 0:disable, 1: emit log if side exit occured */

/* Initial buffer size of lir memory allocator */
#define LIR_COMPILE_DATA_BUFF_SIZE (512)

#define HOT_TRACE_THRESHOLD 2
#define BLACKLIST_TRACE_THRESHOLD 8

#define JIT_ENABLE_LINK_TO_CHILD_TRACE 1

#define MAX_TRACE_SIZE 256

/* optimization flags */
/* trace selection optimization flags */
#define JIT_USE_BLOOM_FILTER 1

/* LIR optimization flags */
#define LIR_OPT_PEEPHOLE_OPTIMIZATION 1
#define LIR_OPT_CONSTANT_FOLDING 1
#define LIR_OPT_DEAD_CODE_ELIMINATION 1
#define LIR_OPT_INST_COMBINE 1
#define LIR_OPT_INST_COMBINE_STACK_OP 1
#define LIR_OPT_LOOP_INVARIANT_CODE_MOTION 0
#define LIR_OPT_ESCAPE_ANALYSIS 1
#define LIR_OPT_RANGE_ANALYSIS 1
#define LIR_OPT_BLOCK_GUARD_MOTION 1

#define ENABLE_PROFILE_TRACE_JIT 1
#endif /* end of include guard */
