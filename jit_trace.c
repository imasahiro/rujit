/**********************************************************************

  jit_trace.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

// record/invoke trace
void rujit_record_insn(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    rujit_t *jit = current_jit;
    trace_recorder_t *recorder = jit->recorder;
    if (UNLIKELY(disable_jit || th != jit->main_thread)) {
	return;
    }
    assert(is_recording(jit));
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    if (is_end_of_trace(recorder, e)) {
	rujit_push_compile_queue(jit, recorder->cur_func);
	stop_recording(jit);
    }
    else {
	record_insn(recorder, e);
    }
}

int rujit_invoke_or_make_trace(rb_thread_t *th, rb_control_frame_t *reg_cfp, VALUE *reg_pc)
{
    jit_event_t ebuf, *e;
    rujit_t *jit = current_jit;
    jit_trace_t *trace;
    if (UNLIKELY(disable_jit || th != jit->main_thread)) {
	return 0;
    }
    if (is_recording(jit)) {
	return 0;
    }
    e = jit_event_init(&ebuf, jit, th, reg_cfp, reg_pc);
    trace = find_trace(jit, e);

    if (trace_is_compiled(trace)) {
	return trace_invoke(jit, e, trace);
    }
    if (is_backward_branch(e)) {
	if (trace == NULL) {
	    trace = rujit_alloc_trace(jit, e, NULL);
	}
	trace->start_pc = reg_pc;
    }

    if (trace) {
	trace->counter += 1;
	if (trace->counter > HOT_TRACE_THRESHOLD) {
	    trace_recorder_t *recorder = jit->recorder;
	    if (find_trace_in_blacklist(jit, trace)) {
		return 0;
	    }
	    start_recording(jit, trace);
	    trace_reset(trace);
	    trace_recorder_reset(recorder, trace, 1);
	    trace_recorder_create_entry_block(recorder, reg_pc);
	    record_insn(recorder, e);
	}
    }
    return 0;
}

// compile method
void rujit_push_compile_queue(rb_thread_t *th, rb_control_frame_t *cfp, rb_method_entry_t *me)
{
    assert(0 && "not implemented");
}
