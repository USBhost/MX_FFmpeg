/*
 *  libzvbi VPS/PDC example 2
 *
 *  Copyright (C) 2009 Michael H. Schimek
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id: pdc2.c,v 1.1 2009/03/23 01:30:39 mschimek Exp $ */

/* This example shows how to receive and decode VPS/PDC Program IDs.
   For simplicity channel change functions have been omitted and not
   all PDC features are supported. (A more complete example will be
   added later.)

   To compile this program type:
   gcc -o pdc2 pdc2.c `pkg-config zvbi-0.2 --cflags --libs`

   This program expects the starting date and time, ending time
   and VPS/PDC time of a TV program to record as arguments:
   ./pdc2  YYYY-MM-DD HH:MM  HH:MM  HH:MM

   It opens a V4L2 device at /dev/vbi and scans the currently tuned in
   channel for a matching VPS/PDC label, logging the progress on
   standard output, without actually recording anything.

   The -t option enables a test mode where the program reads VPS/PDC
   signal changes from standard input instead of opening a VBI
   device. See parse_test_file_line() for a description of the file
   format.
*/

#define _GNU_SOURCE 1
#undef NDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

#include <libzvbi.h>

#ifndef N_ELEMENTS
#  define N_ELEMENTS(array) (sizeof (array) / sizeof (*(array)))
#endif
#ifndef MIN
#  define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static vbi_capture *		cap;
static vbi_decoder *		dec;
static const char *		dev_name;
static vbi_bool			quit;
static int			exit_code;

/* The current time of the intended audience of the tuned in network
   according to the network (see VBI_EVENT_LOCAL_TIME). It may differ
   from system time if the system is not in sync with UTC or if we
   receive the TV signal with a delay. */
static time_t			audience_time;

/* The system time in seconds when the most recent PDC signal was
   received. */
static double			timestamp;

/* PDC Label Channel state. */
struct lc_state {
	/* The PIL most recently received on this LC, zero if none. */
	vbi_pil				pil;

	/* The system time in seconds when the PIL was most recently
	   received. */
	double				last_at;
};

/* The most recently received PILs. */
static struct lc_state		lc_state[VBI_MAX_PID_CHANNELS];

/* Video recorder states. */
enum vcr_state {
	/* All capturing stopped. */
	VCR_STATE_STBY,

	/* Searching for a PDC signal. */
	VCR_STATE_SCAN,

	/* Preparing to record. */
	VCR_STATE_PTR,

	/* Recording a program. */
	VCR_STATE_REC
};

/* The current video recorder state. */
static enum vcr_state		vcr_state;

/* The system time in seconds at the last change of vcr_state. */
static double			vcr_state_since;

/* In timer control mode we start and stop recording at the scheduled
   times. Timer control mode is enabled when the network does not
   transmit program IDs or when we lost all PDC signals. */
static vbi_bool			timer_control_mode;

/* In VCR_STATE_REC this variable stops recording with a 30 second
   delay as required by EN 300 231. This is a system time in
   seconds, or DBL_MAX if no stop is planned. */
static double			delayed_stop_at;

/* In VCR_REC_STATE if delayed_stop_at < DBL_MAX, delayed_stop_pid
   contains a copy of the program ID which caused the delayed stop.

   If delayed_stop_pid.luf == 1 the program will continue on the
   channel with delayed_stop_pid.cni, accompanied by
   delayed_stop_pid.pil (which may also provide a new start date and
   time for the schedule).

   Otherwise delayed_stop_pid.pil can be a valid PIL, a RI/T or INT
   service code, or zero if a loss of the PDC signal or service caused
   the delayed stop. */
static vbi_program_id		delayed_stop_pid;

/* A program to be recorded. */
struct program {
	struct program *	next;

	/* A number in lieu of a title. */
	unsigned int		index;

	/* The most recently announced start and end time of the
	   program ("AT-1" in EN 300 231 in parlance), in case we do
	   not receive a PDC signal. When the duration of the program
	   is unknown start_time == end_time. end_time is
	   exclusive. */
	time_t			start_time;
	time_t			end_time;

	/* The expected Program Identification Label. Usually this is
	   the originally announced start date and time of the program
	   ("AT-2" in EN 300 231), relative to the time zone of the
	   intended audience. */
	vbi_pil			pil;

	/* The validity window of pil, that is the time when the
	   network can be expected to transmit the PIL. Usually from
	   00:00 on the same day to 04:00 on the next
	   day. pil_valid_end is exclusive. */
	time_t			pil_valid_start;
	time_t			pil_valid_end;

	/* Recording is in progress or was interrupted. */
	vbi_bool		continues;
};

/* The recording schedule, a singly-linked list of program
   structures. */
static struct program *		schedule;

/* In VCR_STATE_PTR and VCR_STATE_REC the program we (are about to)
   record, a pointer into the schedule list. Otherwise NULL. */
static struct program *		curr_program;

/* If curr_program != NULL this variable contains a copy of the
   program ID which put us into PTR or REC state. If recording was
   started by the timer curr_pid.pil is zero. */
static vbi_program_id		curr_pid;

static vbi_bool			test_mode;

/* In test mode this is the expected VCR state after the most recent
   PDC signal change. */
static enum vcr_state		test_exp_vcr_state;

static const double
signal_timeout [VBI_MAX_PID_CHANNELS] = {
	[VBI_PID_CHANNEL_LCI_0] = 2,
	[VBI_PID_CHANNEL_LCI_1] = 2,
	[VBI_PID_CHANNEL_LCI_2] = 2,
	[VBI_PID_CHANNEL_LCI_3] = 2,

	/* VPS signals have no error protection. When the payload
	   changes, libzvbi will wait for one repetition to confirm
	   correct reception. */
	[VBI_PID_CHANNEL_VPS] = 3 / 25.0,

	/* Other channels not implemented yet. */
};

static const double
signal_period [VBI_MAX_PID_CHANNELS] = {
	/* EN 300 231 Section 8.3: "In the case of the packet 8/30
	   version (Method B) the repetition rate of labels in any
	   label data channel is once per second." Section E.2: "Where
	   more than one label channel is in use the signalling rate
	   is normally one line per label channel per second." */
	[VBI_PID_CHANNEL_LCI_0] = 1,
	[VBI_PID_CHANNEL_LCI_1] = 1,
	[VBI_PID_CHANNEL_LCI_2] = 1,
	[VBI_PID_CHANNEL_LCI_3] = 1,

	[VBI_PID_CHANNEL_VPS] = 1 / 25.0,

	/* Other channels not implemented yet. */
};

/* For debugging. */
#define D printf ("%s:%u\n", __FILE__, __LINE__)

/* For debugging. */
static void
print_time			(time_t			time)
{
	char buffer[80];
	struct tm tm;

	memset (&tm, 0, sizeof (tm));
	localtime_r (&time, &tm);
	strftime (buffer, sizeof (buffer),
		  "%Y-%m-%d %H:%M:%S %Z = ", &tm);
	fputs (buffer, stdout);

	memset (&tm, 0, sizeof (tm));
	gmtime_r (&time, &tm);
	strftime (buffer, sizeof (buffer),
		  "%Y-%m-%d %H:%M:%S UTC", &tm);
	puts (buffer);
}

/* Attention! This function returns a static string. */
static const char *
pil_str				(vbi_pil		pil)
{
	static char buffer[32];

	switch (pil) {
	case VBI_PIL_TIMER_CONTROL:	return "TC";
	case VBI_PIL_INHIBIT_TERMINATE:	return "RI/T";
	case VBI_PIL_INTERRUPTION:	return "INT";
	case VBI_PIL_CONTINUE:		return "CONT";

	case VBI_PIL_NSPV:
		/* NSVP service code if source is VPS/PDC,
		   END code if source is XDS. */
		return "NSPV/END";

	default:
		snprintf (buffer, sizeof (buffer),
			  "%02u%02uT%02u%02u",
			  VBI_PIL_MONTH (pil),
			  VBI_PIL_DAY (pil),
			  VBI_PIL_HOUR (pil),
			  VBI_PIL_MINUTE (pil));

		return buffer;
	}
}

static void
msg				(const char *		templ,
				 ...)
{
	va_list ap;

	va_start (ap, templ);

	if (test_mode) {
		char buffer[80];
		struct tm tm;

		memset (&tm, 0, sizeof (tm));
		localtime_r (&audience_time, &tm);
		strftime (buffer, sizeof (buffer), "%Y%m%dT%H%M%S ", &tm);
		fputs (buffer, stdout);
	}

	vprintf (templ, ap);

	va_end (ap);
}

static void
remove_program_from_schedule	(struct program *	p)
{
	struct program **pp;

	if (p == curr_program) {
		assert (quit
			|| VCR_STATE_STBY == vcr_state
			|| VCR_STATE_SCAN == vcr_state);

		curr_program = NULL;
	}

	for (pp = &schedule; NULL != *pp; pp = &(*pp)->next) {
		if (*pp == p) {
			*pp = p->next;
			free (p);
			break;
		}
	}
}

static void
remove_stale_programs_from_schedule (void)
{
	struct program *p;
	struct program *p_next;

	for (p = schedule; NULL != p; p = p_next) {
		p_next = p->next;

		if (audience_time >= p->end_time
		    && audience_time >= p->pil_valid_end) {
			msg ("PIL %s no longer valid, "
			     "removing program %u from schedule.\n",
			     pil_str (p->pil), p->index);
			remove_program_from_schedule (p);
		}
	}
}

static struct program *
find_program_by_pil		(vbi_pil		pil)
{
	struct program *p;

	for (p = schedule; NULL != p; p = p->next) {
		if (pil == p->pil)
			return p;
	}

	return NULL;
}

static const char *
vcr_state_name			(enum vcr_state		state)
{
	switch (state) {
#define CASE(x) case VCR_STATE_ ## x: return #x;
	CASE (STBY)
	CASE (SCAN)
	CASE (PTR)
	CASE (REC)
#undef CASE
	}

	assert (0);
}

static void
change_vcr_state		(enum vcr_state		new_state)
{
	if (new_state == vcr_state)
		return;

	msg ("VCR state %s -> %s.\n",
	     vcr_state_name (vcr_state),
	     vcr_state_name (new_state));

	vcr_state = new_state;
	vcr_state_since = timestamp;
}

static vbi_bool
teletext_8302_available		(void)
{
	return (0 != (lc_state[VBI_PID_CHANNEL_LCI_0].pil |
		      lc_state[VBI_PID_CHANNEL_LCI_1].pil |
		      lc_state[VBI_PID_CHANNEL_LCI_2].pil |
		      lc_state[VBI_PID_CHANNEL_LCI_3].pil));
}

static void
disable_timer_control		(void)
{
	if (!timer_control_mode)
		return;
	msg ("Leaving timer control mode.\n");
	timer_control_mode = FALSE;
}

static void
enable_timer_control		(void)
{
	if (timer_control_mode)
		return;
	msg ("Entering timer control mode.\n");
	timer_control_mode = TRUE;
}

static void
stop_recording_now		(void)
{
	assert (VCR_STATE_REC == vcr_state);

	msg ("Program %u ended according to %s%s.\n",
	     curr_program->index,
	     timer_control_mode ? "schedule" : "VPS/PDC signal",
	     (delayed_stop_at < DBL_MAX) ? " with delay" : "");

	change_vcr_state (VCR_STATE_SCAN);

	delayed_stop_at = DBL_MAX;
}

static void
stop_recording_in_30s		(const vbi_program_id *	pid)
{
	assert (VCR_STATE_REC == vcr_state);

	/* What triggered the stop. */
	if (NULL == pid) {
		/* Signal lost. */
		memset (&delayed_stop_pid, 0,
			sizeof (delayed_stop_pid));
	} else {
		delayed_stop_pid = *pid;
	}

	/* If we stop because the PIL is no longer transmitted we may
	   need one second to realize (e.g. receiving LCI 0 at time t,
	   LCI 1 at t + 0.2, then LCI 0 at t + 1, and again LCI 0 at
	   t + 2 seconds) so we start counting 30 seconds not from
	   the current time (t + 2) but the first time the label was
	   missing (t + 1). */
	if (NULL == pid && 0 != curr_pid.pil) {
		delayed_stop_at = lc_state[curr_pid.channel].last_at + 31;
	} else {
		delayed_stop_at = timestamp + 30;
	}

	msg ("Will stop recording in %d seconds.\n",
	     (int)(delayed_stop_at - timestamp));
}

static void
start_recording_by_pil		(struct program *	p,
				 const vbi_program_id *	pid)
{
	assert (!timer_control_mode);
	assert (VCR_STATE_SCAN == vcr_state
		|| VCR_STATE_PTR == vcr_state);

	msg ("Recording program %u using VPS/PDC signal.\n",
	     p->index);

	/* EN 300 231 Section 9.4.1: "[When] labels are not received
	   correctly during a recording, the recording will be
	   continued for the computed duration following the actual
	   start time" */
	if (!p->continues) {
		p->end_time += audience_time - p->start_time;
		p->start_time = audience_time;
		p->continues = TRUE;
	}

	change_vcr_state (VCR_STATE_REC);

	curr_program = p;
	curr_pid = *pid;
}

static void
prepare_to_record_by_pil	(struct program *	p,
				 const vbi_program_id *	pid)
{
	assert (!timer_control_mode);
	assert (VCR_STATE_SCAN == vcr_state);

	change_vcr_state (VCR_STATE_PTR);

	curr_program = p;
	curr_pid = *pid;
}

static void
start_recording_by_timer	(struct program *	p)
{
	assert (timer_control_mode);
	assert (VCR_STATE_SCAN == vcr_state);

	msg ("Recording program %u using timer.\n",
	     p->index);

	change_vcr_state (VCR_STATE_REC);

	curr_program = p;
	memset (&curr_pid, 0, sizeof (curr_pid));
}

static void
remove_program_if_ended		(struct	program *	p,
				 const vbi_program_id * pid)
{
	if (timer_control_mode) {
		/* We don't know if the program really ends now, so we
		   keep it scheduled until curr_program->pil_valid_end
		   in case we receive its PIL after all. */
		return;
	} else if (NULL != pid && VBI_PIL_INTERRUPTION == pid->pil) {
		/* The program pauses, will not be removed. */
		return;
	} else if (NULL != pid && pid->luf) {
		/* The program has been rescheduled to another date,
		   we don't care in this example. */
	}

	/* Objective accomplished. */
	remove_program_from_schedule (p);
}

static void
signal_or_service_lost		(void)
{
	struct program *p;

	if (timer_control_mode)
		return;

	enable_timer_control ();

	switch (vcr_state) {
	case VCR_STATE_STBY:
		assert (0);

	case VCR_STATE_SCAN:
		break;

	case VCR_STATE_PTR:
		p = curr_program;

		/* According to EN 300 231 Section E.1 and Section E.3
		   Example 12 the program should begin within one
		   minute when PRF=1, so we start recording now. We
		   will stop by PIL if we pick up a VPS or Teletext
		   signal again before curr_program->end_time, but we
		   will not return to VCR_STATE_PTR if PRF is still
		   1. */
		msg ("Recording program %u using lost "
		     "PDC signal with PRF=1.\n",
		     p->index);

		/* Record for the scheduled duration... */
		p->end_time = p->end_time - p->start_time + audience_time;
		/* ...plus one minute since PRF was set. */
		p->end_time += 60 - MIN (vcr_state_since - timestamp,
				         60.0);
		p->start_time = audience_time;

		change_vcr_state (VCR_STATE_REC);

		/* Now recording by timer. */
		memset (&curr_pid, 0, sizeof (curr_pid));

		break;

	case VCR_STATE_REC:
		if (delayed_stop_at < DBL_MAX) {
			msg ("PDC signal lost; already stopping in "
			     "%d seconds.\n",
			     (int)(delayed_stop_at - timestamp));
		} else if (curr_program->start_time
			   == curr_program->end_time) {
			/* Since we don't know the program duration,
			   we cannot record under timer control. We
			   stop recording in 30 seconds as shown in EN
			   300 231 Annex E.3, Example 11, 16:20:10,
			   but with an extra twist: If we receive
			   curr_program->pil again within those 30
			   seconds the stop will be canceled. */
			stop_recording_in_30s (/* pid */ NULL);
		} else {
			/* Keep recording by timer. */
			memset (&curr_pid, 0, sizeof (curr_pid));
		}

		break;
	}
}

static void
pil_no_longer_transmitted	(const vbi_program_id *	pid)
{
	vbi_bool mi;

	switch (vcr_state) {
	case VCR_STATE_STBY:
	case VCR_STATE_SCAN:
		assert (0);

	case VCR_STATE_PTR:
		assert (!timer_control_mode);

		msg ("PIL %s is no longer present on LC %u.\n",
		     pil_str (curr_program->pil),
		     curr_pid.channel);

		change_vcr_state (VCR_STATE_SCAN);

		return;

	case VCR_STATE_REC:
		assert (!timer_control_mode);

		msg ("PIL %s is no longer present on LC %u.\n",
		     pil_str (curr_program->pil),
		     curr_pid.channel);

		if (delayed_stop_at < DBL_MAX) {
			msg ("Already stopping in %d seconds.\n",
			     (int)(delayed_stop_at - timestamp));
			return;
		}

		break;
	}

	if (NULL != pid
	    /* EN 300 231 Annex E.3 Example 8. */
	    && !pid->luf
	    /* EN 300 231 Section 6.2 p) and Annex E.3 Example 7 and
	       9. */
	    && (VBI_PIL_INTERRUPTION == pid->pil
		|| VBI_PIL_INHIBIT_TERMINATE == pid->pil)) {
		mi = pid->mi;
	} else {
		/* EN 300 231 is unclear about the expected response
		   if a PIL with MI = 1 replaces a PIL with MI = 0 or
		   vice versa. Section 6.2 p) suggests that only the
		   MI flag of the old label determines when the
		   program stops and Annex E.3 Example 1 to 7 are
		   consistent with this interpretation, Example 10 is
		   not. */
		if (0 == curr_pid.pil) {
			/* Recording was started by timer. */
			mi = TRUE;
		} else {
			mi = curr_pid.mi;
		}
	}

	if (mi) {
		stop_recording_now ();
		remove_program_if_ended (curr_program, pid);
	} else {
		stop_recording_in_30s (pid);
	}
}

/* Interruption or Recording Inhibit/Terminate service code. */
static void
received_int_rit		(const vbi_program_id *	pid)
{
	switch (vcr_state) {
	case VCR_STATE_STBY:
		assert (0);

	case VCR_STATE_SCAN:
		disable_timer_control ();
		return;

	case VCR_STATE_PTR:
		assert (!timer_control_mode);

		if (pid->channel != curr_pid.channel) {
			msg ("Ignore %s/%02X with different LCI.\n",
			     pil_str (pid->pil), pid->pty);
			return;
		}

		break;

	case VCR_STATE_REC:
		if (timer_control_mode) {
			/* Impossible to know if this service code
			   refers to curr_program, so we keep
			   recording for now. */
			return;
		} else if (pid->channel != curr_pid.channel) {
			msg ("Ignore %s/%02X with different LCI.\n",
			     pil_str (pid->pil), pid->pty);
			return;
		}

		break;
	}

	pil_no_longer_transmitted (pid);
}

static void
received_pil			(const vbi_program_id *	pid)
{
	struct program *p;

	switch (vcr_state) {
	case VCR_STATE_STBY:
		assert (0);

	case VCR_STATE_SCAN:
		disable_timer_control ();
		if (pid->luf)
			return;
		p = find_program_by_pil (pid->pil);
		break;

	case VCR_STATE_PTR:
		assert (!timer_control_mode);

		if (pid->channel != curr_pid.channel) {
			msg ("Ignore %s/%02X with different LCI.\n",
			     pil_str (pid->pil), pid->pty);
			return;
		} else if (pid->luf) {
			pil_no_longer_transmitted (pid);

			/* This example does not support VCR
			   reprogramming. */
			return;
		} else if (pid->pil != curr_pid.pil) {
			pil_no_longer_transmitted (pid);
			p = find_program_by_pil (pid->pil);
			break;
		} else if (pid->prf) {
			if (timestamp >= vcr_state_since + 60) {
				/* EN 300 231 Section E.1,
				   Section E.3 Example 12. */
				msg ("Overriding stuck PRF flag.\n");
			} else {
				msg ("Already prepared to record.\n");
				return;
			}
		}

		/* PRF 1 -> 0, program starts now. */

		start_recording_by_pil (curr_program, pid);

		return;

	case VCR_STATE_REC:
		if (timer_control_mode) {
			if (pid->luf) {
				/* Impossible to know if this service
				   code refers to curr_program, so we
				   keep recording for now. */
				return;
			}

			p = find_program_by_pil (pid->pil);
			if (p == curr_program) {
				disable_timer_control ();

				msg ("Continue recording using "
				     "VPS/PDC signal.\n");

				curr_pid = *pid;

				/* Cancel a delayed stop because the
				   program is evidently still
				   running. */
				delayed_stop_at = DBL_MAX;

				return;
			} else if (NULL == p) {
				/* This program is not scheduled for
				   recording but the network may
				   transmit other PILs in parallel, so
				   we allow some time to pick them up
				   before we stop. */
				stop_recording_in_30s (/* pil */ NULL);
				return;
			} else {
				disable_timer_control ();

				/* Perhaps in practice one should just
				   open a new file and not restart
				   capturing. */
				stop_recording_now ();
			}
		} else if (pid->channel != curr_pid.channel) {
			msg ("Ignore %s/%02X with different LCI.\n",
			     pil_str (pid->pil), pid->pty);
			return;
		} else if (pid->luf) {
			pil_no_longer_transmitted (pid);

			/* This example does not support VCR
			   reprogramming. */
			return;
		} else if (pid->pil == curr_pid.pil) {
			if (delayed_stop_at < DBL_MAX) {
				/* We lost all PDC signals and
				   timer_control() arranged for a
				   delayed stop. Or we received an INT
				   or RI/T code or a different PIL
				   than curr_program->pil with
				   MI=0. But now we receive
				   curr_program->pil again. */
				delayed_stop_at = DBL_MAX;
				msg ("Delayed stop canceled.\n");
				return;
			} else {
				/* We lost all PDC signals and
				   timer_control() started recording
				   out of SCAN or PTR state, but now
				   we receive curr_program->pil
				   (again). Or this is just a
				   retransmission of the PIL which
				   started recording. Either way, we
				   do not return to VCR_STATE_PTR if
				   PRF is (still or again) 1. */
				msg ("Already recording.\n");
				return;
			}
		} else {
			pil_no_longer_transmitted (pid);
			if (VCR_STATE_SCAN != vcr_state) {
				/* Stopping later. */
				return;
			}

			p = find_program_by_pil (pid->pil);
		}

		break;
	}

	assert (VCR_STATE_SCAN == vcr_state);

	if (NULL == p)
		return;

	if (pid->prf) {
		prepare_to_record_by_pil (p, pid);
	} else {
		start_recording_by_pil (p, pid);
	}
}

static void
event_handler			(vbi_event *		ev,
				 void *			user_data)
{
	const vbi_program_id *pid;
	vbi_pid_channel lci;

	user_data = user_data; /* unused, no warning please */

	assert (VCR_STATE_STBY != vcr_state);

	pid = ev->ev.prog_id;
	lci = pid->channel;

	switch (lci) {
	case VBI_PID_CHANNEL_LCI_0:
	case VBI_PID_CHANNEL_LCI_1:
	case VBI_PID_CHANNEL_LCI_2:
	case VBI_PID_CHANNEL_LCI_3:
		break;

	case VBI_PID_CHANNEL_VPS:
		/* EN 300 231 Section 9.4.1: "When both line 16 (VPS)
		   and Teletext-delivered labels are available
		   simultaneously, decoders should default to the
		   Teletext-delivered service;" */
		if (teletext_8302_available ())
			goto finish;
		break;

	default:
		/* Support for other sources not implemented yet. */
		return;
	}

	msg ("Received PIL %s/%02X on LC %u.\n",
	     pil_str (pid->pil), pid->pty, lci);

	switch (pid->pil) {
	case VBI_PIL_TIMER_CONTROL:
	case VBI_PIL_CONTINUE:
		signal_or_service_lost ();
		break;

	case VBI_PIL_INTERRUPTION:
	case VBI_PIL_INHIBIT_TERMINATE:
		received_int_rit (pid);
		break;

	default:
		received_pil (pid);
		break;
	}

 finish:
	lc_state[lci].pil = pid->pil;
	lc_state[lci].last_at = timestamp;
}

static vbi_bool
in_pil_validity_window		(void)
{
	struct program *p;

	for (p = schedule; NULL != p; p = p->next) {
		/* The announced start and end time should fall within
		   the PIL validity window, but just in case. */
		if ((audience_time >= p->start_time
		     && audience_time < p->end_time)
		    || (audience_time >= p->pil_valid_start
			&& audience_time < p->pil_valid_end))
			return TRUE;
	}

	return FALSE;
}

static void
timer_control			(void)
{
	struct program *p;

	assert (timer_control_mode);

	switch (vcr_state) {
	case VCR_STATE_STBY:
	case VCR_STATE_PTR:
		assert (0);

	case VCR_STATE_SCAN:
		break;

	case VCR_STATE_REC:
		if (delayed_stop_at < DBL_MAX) {
			/* Will stop later. */
			return;
		} else if (audience_time >= curr_program->end_time) {
			stop_recording_now ();

			/* We remove the program from the schedule as
			   shown in EN 300 231 Annex E.3, Example 11,
			   01:58:00. However as the example itself
			   demonstrates this is not in the best
			   interest of the user. A better idea may be
			   to keep the program scheduled until
			   curr_program->pil_valid_end, in case the
			   program is late or overrunning and we
			   receive its PIL after all. */
			remove_program_from_schedule (curr_program);
		} else {
			/* Still running. */
			return;
		}

		assert (VCR_STATE_SCAN == vcr_state);

		break;
	}

	for (p = schedule; NULL != p; p = p->next) {
		/* Note if no program length has been specified
		   (start_time == end_time) this function will not
		   record the program. */
		/* We must also compare against p->end_time because we
		   will not always remove the program from the
		   schedule at that time. See
		   remove_program_if_ended(). */
		if (audience_time >= p->start_time
		    && audience_time < p->end_time) {
			start_recording_by_timer (p);
			return;
		}
	}
}

static void
pdc_signal_check		(void)
{
	static const unsigned int ttx_chs =
		((1 << VBI_PID_CHANNEL_LCI_0) |
		 (1 << VBI_PID_CHANNEL_LCI_0) |
		 (1 << VBI_PID_CHANNEL_LCI_0) |
		 (1 << VBI_PID_CHANNEL_LCI_0));
	static const unsigned int vps_ch =
		(1 << VBI_PID_CHANNEL_VPS);
	unsigned int active_chs;
	unsigned int lost_chs;
	vbi_pid_channel i;

	if (timer_control_mode)
		return;

	/* Determine if we lost signals. */

	active_chs = 0;
	lost_chs = 0;

	for (i = 0; i < VBI_MAX_PID_CHANNELS; ++i) {
		double timeout_at;

		if (0 == lc_state[i].pil)
			continue;

		timeout_at = lc_state[i].last_at + signal_timeout[i];
		if (timestamp >= timeout_at) {
			lost_chs |= 1 << i;
		} else {
			active_chs |= 1 << i;
		}
	}

	/* For now only Teletext and VPS delivery is supported, so we
	   don't check other channels. */

	if (0 == active_chs) {
		if (0 != lost_chs) {
			msg ("All Teletext and VPS signals lost, "
			     "will fall back to timer control.\n");

			signal_or_service_lost ();
		}
	} else {
		if (vps_ch == active_chs
		    && 0 != (lost_chs & ttx_chs)) {
			msg ("Teletext signal lost, "
			     "will fall back to VPS.\n");

			if (curr_pid.pil
			    == lc_state[VBI_PID_CHANNEL_VPS].pil) {
				curr_pid.channel = VBI_PID_CHANNEL_VPS;
			}
		}

		if ((VCR_STATE_PTR == vcr_state
		     || VCR_STATE_REC == vcr_state)
		    && 0 != curr_pid.pil
		    && 0 != (lost_chs & (1 << curr_pid.channel))) {
			/* Note if multiple label channels are in use
			   (Teletext only) a PIL may just "disappear"
			   without a RI/T service code or other PIL
			   subsequently transmitted on the same
			   channel. */
			pil_no_longer_transmitted (/* pid */ NULL);
		}
	}

	if (0 != lost_chs) {
		for (i = 0; i < VBI_MAX_PID_CHANNELS; ++i) {
			if (0 == (lost_chs & (1 << i)))
				continue;

			lc_state[i].pil = 0;
			lc_state[i].last_at = timestamp;
		}
	}
}

static void
parse_test_file_line		(time_t *		timestamp,
				 vbi_program_id *	pid,
				 enum vcr_state *	exp_state,
				 unsigned int		line_counter,
				 const char *		test_file_line)
{
	struct tm tm;
	const char *s;
	char *s_end;
	const char *detail;
	unsigned long ul;

	/* Test file format (based on examples in EN 300 231):

	   One line of text for each PID change, with 4 or 9 fields
	   separated by one or more tabs or spaces:

	   1. Name of the broadcasting network, e.g. BBC1.
	   2. Date of the change: yyyymmddThhmmss (local time)
                              or  yyyymmddThhmmssZ (UTC)
	      Lines must be sorted by this date, oldest first. Dates
	      must not repeat unless these lines have different LCI
	      fields.
	   3. Label Channel Identifier (vbi_pid_channel): 0 ... n
	      or the name VPS (channel 4).
	   4. Label Update Flag: 0 or 1.
	   5. Mode Identifier: 0 or 1 or x (any).
	   6. Prepare to Record Flag: 0 or 1 or x (any).
	   7. Program Identification Label: mmddThhmm or one of the
	      names
	      - TC (Timer Control code)
	      - RI/T (Recording Inhibit/Terminate code)
	      - INT (Interruption code)
	      - CONT (Continuation code)
	      - NSPV (No Specific PIL Value).
              A Program Type can be appended, separated by a slash:
	      - /A to /Z (Series Code)
	      - /NN (a hex number, e.g. /3F)
	   8. Channel or Network Identifier: a name like BBC1.
	   9. Expected VCR state:
	      - STBY
	      - SCAN
	      - PTR
	      - REC.

	   If fields 4 to 8 are omitted the transmission of the label
	   on the given label channel ceases. If field 9 is omitted
	   the same VCR state as before is expected.

	   All text after a number sign (#) is ignored.
	*/

	s = test_file_line;

	/* Network name ignored in this example. */
	while (isalnum (*s))
		++s;
	detail = "channel field";
	if (!isspace (*s))
		goto invalid;

	memset (&tm, 0, sizeof (tm));
	tm.tm_isdst = -1; /* unknown */

	s = strptime (s, "%n%Y%m%dT%H%M%S", &tm);
	detail = "date field";
	if (NULL == s)
		goto invalid;
	while (isspace (*s))
		++s;
	if ('Z' == *s) {
		++s;
		*timestamp = timegm (&tm);
	} else {
		*timestamp = mktime (&tm);
	}
	if ((time_t) -1 == *timestamp)
		goto invalid;

	memset (pid, 0, sizeof (*pid));

	while (isspace (*s))
		++s;
	if (0 == strncmp (s, "VPS", 3)) {
		pid->channel = VBI_PID_CHANNEL_VPS;
		s += 3;
	} else {
		ul = strtoul (s, &s_end, 0);
		detail = "LCI field";
		if (s_end == s
		    || ul >= (unsigned long) VBI_MAX_PID_CHANNELS)
			goto invalid;
		pid->channel = ul;
		s = s_end;
	}

	while (isspace (*s))
		++s;
	if (!isdigit (*s)) {
		/* Cease transmission on this label channel,
		   pid->pil = 0. */
	} else {
		ul = strtoul (s, &s_end, 0);
		detail = "LUF field";
		if (s_end == s || ul > 1)
			goto invalid;
		pid->luf = ul;
		s = s_end;

		while (isspace (*s))
			++s;
		if ('x' == *s) {
			++s;
		} else {
			ul = strtoul (s, &s_end, 0);
			detail = "MI field";
			if (s_end == s || ul > 1)
				goto invalid;
			pid->mi = ul;
			s = s_end;
		}

		while (isspace (*s))
			++s;
		if ('x' == *s) {
			++s;
		} else {
			ul = strtoul (s, &s_end, 0);
			detail = "PRF field";
			if (s_end == s || ul > 1)
				goto invalid;
			pid->prf = ul;
			s = s_end;
		}

		while (isspace (*s))
			++s;
		if (0 == strncmp (s, "CONT", 4)) {
			pid->pil = VBI_PIL_CONTINUE;
			s += 4;
		} else if (0 == strncmp (s, "END", 3)) {
			pid->pil = VBI_PIL_END;
			s += 3;
		} else if (0 == strncmp (s, "INT", 3)) {
			pid->pil = VBI_PIL_INTERRUPTION;
			s += 3;
		} else if (0 == strncmp (s, "NSPV", 4)) {
			pid->pil = VBI_PIL_NSPV;
			s += 4;
		} else if (0 == strncmp (s, "RI/T", 4)) {
			pid->pil = VBI_PIL_INHIBIT_TERMINATE;
			s += 4;
		} else if (0 == strncmp (s, "TC", 2)) {
			pid->pil = VBI_PIL_TIMER_CONTROL;
			s += 2;
		} else {
			ul = strtoul (s, &s_end, 10);
			detail = "PIL field";
			if (s_end == s
			    || ul % 100 > 31
			    || ul > 1531)
				goto invalid;
			s = s_end;
			if (ul > 0) {
				pid->pil = VBI_PIL (ul / 100,
						    ul % 100, 0, 0);
				if ('T' != *s++)
					goto invalid;
				ul = strtoul (s, &s_end, 10);
				if (s_end == s
				    || ul % 100 > 63
				    || ul > 3163)
					goto invalid;
				s = s_end;
				pid->pil |= VBI_PIL (0, 0,
						     ul / 100,
						     ul % 100);
			}
		}

		if ('/' == *s) {
			do ++s;
			while (isspace (*s));
			if (isalpha (s[0]) && 0x20 == s[1]) {
				/* Series code. This isn't magic, EN
				   300 231 just gives letters instead
				   of the codes 0x80 ... 0xFF for
				   easier reading. */
				pid->pty = 0x80 | *s++;
			} else {
				ul = strtoul (s, &s_end, 16);
				detail = "PTY field";
				if (s_end == s || ul > 0xFF)
					goto invalid;
				pid->pty = ul;
				s = s_end;
			}
		} else {
			pid->pty = 0;
		}

		/* Network name ignored in this example. */
		while (isspace (*s))
			++s;
		while (isalnum (*s))
			++s;
		detail = "CNI field";
		if (0 != *s && !isspace (*s))
			goto invalid;
		if (VBI_PID_CHANNEL_VPS == pid->channel) {
			pid->cni_type = VBI_CNI_TYPE_VPS;
			pid->cni = 0x1234;
		} else {
			pid->cni_type = VBI_CNI_TYPE_8302;
			pid->cni = 0x1234;
		}
	}

	while (isspace (*s))
		++s;
	if ('#' == *s || 0 == *s) {
		*exp_state = -1; /* no change */
		return;
	} else if (0 == strncmp (s, "PTR", 3)) {
		*exp_state = VCR_STATE_PTR;
		s += 3;
	} else if (0 == strncmp (s, "REC", 3)) {
		*exp_state = VCR_STATE_REC;
		s += 3;
	} else if (0 == strncmp (s, "SCAN", 4)) {
		*exp_state = VCR_STATE_SCAN;
		s += 4;
	} else if (0 == strncmp (s, "STBY", 4)) {
		*exp_state = VCR_STATE_STBY;
		s += 4;
	} else {
		detail = "VCR state field";
		goto invalid;
	}

	while (isspace (*s))
		++s;

	if ('#' == *s || 0 == *s)
		return;

	detail = "garbage at end of line";

 invalid:
	fprintf (stderr, "Error in test file line %u, %s:\n%s\n",
		 line_counter, detail, test_file_line);
	exit (EXIT_FAILURE);
}

static void
simulate_signals		(void)
{
	static char buffer[256];
	static vbi_program_id test_pid[VBI_MAX_PID_CHANNELS];
	static vbi_program_id next_pid;
	static time_t next_event_time = 0;
	static enum vcr_state next_exp_vcr_state = (enum vcr_state) -1;
	static unsigned int line_counter;
	vbi_pid_channel i;

	while (timestamp >= next_event_time) {
		if (0 != buffer[0]) {
			printf ("> %s", buffer);
			test_pid[next_pid.channel] = next_pid;
			if ((enum vcr_state) -1 == next_exp_vcr_state)
				test_exp_vcr_state = test_exp_vcr_state;
			else
				test_exp_vcr_state = next_exp_vcr_state;
		}

		for (;;) {
			const char *s;

			if (NULL == fgets (buffer, sizeof (buffer),
					   stdin)) {
				printf ("End of test file.\n");
				next_event_time = INT_MAX;
				quit = TRUE;
				break;
			}

			s = buffer;

			while (isspace (*s))
				++s;
			if (0 == *s)
				continue;

			if ('#' == *s) {
				printf ("> %s", s);
				continue;
			}

			parse_test_file_line (&next_event_time,
					      &next_pid,
					      &next_exp_vcr_state,
					      line_counter, s);
			++line_counter;

			break;
		}
	}

	/* See standby_loop(). */
	audience_time = (time_t) timestamp;

	/* We stop recording before examining the received PIDs so we
	   can respond to a new PID immediately. */
	if (VCR_STATE_REC == vcr_state
	    && timestamp >= delayed_stop_at) {
		stop_recording_now ();
		assert (VCR_STATE_SCAN == vcr_state);
		remove_program_if_ended (curr_program,
					 &delayed_stop_pid);
	}

	/* Note in reality PIDs may arrive in any order, with a delay
	   of several frames between them. */
	for (i = 0; i < VBI_MAX_PID_CHANNELS; ++i) {
		if (0 != test_pid[i].pil) {
			vbi_event ev;

			memset (&ev, 0, sizeof (ev));
			ev.ev.prog_id = &test_pid[i];

			event_handler (&ev, /* user_data */ NULL);
		}
	}
}

static void
capture_and_decode_frame	(void)
{
	struct timeval timeout;
	vbi_capture_buffer *sliced_buffer;
	unsigned int n_lines;
	int r;

	/* Don't wait more than two seconds for the driver
	   to return data. */
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	r = vbi_capture_pull (cap,
			      /* raw_buffer */ NULL,
			      &sliced_buffer,
			      &timeout);
	switch (r) {
	case -1:
		fprintf (stderr,
			 "VBI read error: %s.\n",
			 strerror (errno));
		/* Could be ignored, esp. EIO from some
		   drivers. */
		exit (EXIT_FAILURE);

	case 0: 
		fprintf (stderr, "VBI read timeout\n");
		exit (EXIT_FAILURE);

	case 1: /* success */
		break;

	default:
		assert (0);
	}

	timestamp = sliced_buffer->timestamp;
	n_lines = sliced_buffer->size / sizeof (vbi_sliced);

	/* See standby_loop(). */
	audience_time = (time_t) timestamp;

	/* We stop recording before examining the received PIDs so we
	   can respond to a new PID immediately. */
	if (VCR_STATE_REC == vcr_state
	    && timestamp >= delayed_stop_at) {
		stop_recording_now ();
		assert (VCR_STATE_SCAN == vcr_state);
		remove_program_if_ended (curr_program,
					 &delayed_stop_pid);
	}

	/* Calls event_handler(). */
	vbi_decode (dec, (vbi_sliced *) sliced_buffer->data,
		    n_lines, timestamp);
}

static void
close_vbi_device		(void)
{
	vbi_capture_delete (cap);
	cap = NULL;
}

static void
open_vbi_device			(void)
{
	vbi_service_set services;
	char *errstr;

	services = (VBI_SLICED_TELETEXT_B |
		    VBI_SLICED_VPS);

	cap = vbi_capture_v4l2_new (dev_name,
				    /* buffers */ 5,
				    &services,
				    /* strict */ 0,
				    &errstr,
				    /* verbose */ FALSE);
	if (NULL == cap) {
		fprintf (stderr,
			 "Cannot capture VBI data from %s "
			 "with V4L2 interface:\n"
			 "%s\n",
			 dev_name, errstr);

		free (errstr);

		exit (EXIT_FAILURE);
	}
}

/* We wait in this function until we receive the expected PIL(s) or a
   program starts and ends as scheduled, and record it. */
static void
capture_loop			(void)
{
	double last_timestamp;

	assert (VCR_STATE_STBY == vcr_state);

	if (!test_mode)
		open_vbi_device ();

	/* Reset the VBI decoder. */
	vbi_channel_switched (dec, 0);

	change_vcr_state (VCR_STATE_SCAN);

	last_timestamp = 0;

	while (VCR_STATE_STBY != vcr_state && !quit) {
		if (test_mode) {
			simulate_signals ();
		} else {
			capture_and_decode_frame ();
		}

		/* Once per second is enough. */
		if ((long) last_timestamp != (long) timestamp) {
			if (!timer_control_mode) {
				/* May enable timer control mode. */
				pdc_signal_check ();
			}

			if (timer_control_mode)
				timer_control ();
		}

		last_timestamp = timestamp;

		if (VCR_STATE_SCAN == vcr_state
		    && !in_pil_validity_window ()) {
			change_vcr_state (VCR_STATE_STBY);
		}

		if (test_mode) {
			if ((enum vcr_state) -1 != test_exp_vcr_state
			    && test_exp_vcr_state != vcr_state) {
				printf ("*** Unexpected VCR state %s\n",
					vcr_state_name (vcr_state));

				exit_code = EXIT_FAILURE;
			}

			/* Advance by one second. Note a VPS signal is
			   transmitted on each frame, 25 times per
			   second, but we simulate at most one PID
			   change per second per label channel. */
			++timestamp;
		}
	}

	if (!test_mode)
		close_vbi_device ();
}

/* We wait in this function until the starting time of the earliest
   program on the recording schedule is approaching. */
static void
standby_loop			(void)
{
	while (!quit) {
		struct program *p;
		time_t first_scan;

		assert (VCR_STATE_STBY == vcr_state);

		if (test_mode) {
			/* Simulated current time. */
			audience_time = (time_t) timestamp;
		} else {
			/* The current time of the intended audience
			   of the tuned in network according to the
			   network. It may differ from system time if
			   the system is not in sync with UTC or if we
			   receive the TV signal with a delay. For
			   simplicity we will not determine the offset
			   in this example, see VBI_EVENT_LOCAL_TIME
			   if you want to try that. */
			audience_time = time (NULL);
		}

		remove_stale_programs_from_schedule ();
		if (NULL == schedule) {
			printf ("Recording schedule is empty.\n");
			break;
		}

		first_scan = schedule->start_time;
		for (p = schedule; NULL != p; p = p->next) {
			if (p->start_time < first_scan)
				first_scan = p->start_time;
			if (p->pil_valid_start < first_scan)
				first_scan = p->pil_valid_start;
		}

		while (first_scan > audience_time) {
			char buffer[80];
			struct tm tm;

			memset (&tm, 0, sizeof (tm));
			localtime_r (&first_scan, &tm);
			strftime (buffer, sizeof (buffer),
				  "%Y-%m-%d %H:%M:%S %Z", &tm);

			msg ("Sleeping until %s.\n", buffer);

			if (test_mode) {
				audience_time = first_scan;
				timestamp = first_scan;
			} else {
				/* In a loop because the sleep()
				   function may abort earlier. */
				sleep (first_scan - audience_time);

				audience_time = time (NULL);
			}
		}

		capture_loop ();
	}
}

static void
reset_state			(void)
{
	unsigned int i;

	audience_time = 0.0;
	timestamp = 0.0;

	for (i = 0; i < VBI_MAX_PID_CHANNELS; ++i) {
		lc_state[i].pil = 0; /* none received */
		lc_state[i].last_at = 0.0;
	}

	vcr_state = VCR_STATE_STBY;
	vcr_state_since = 0.0;

	timer_control_mode = FALSE;

	delayed_stop_at = DBL_MAX;

	test_exp_vcr_state = (enum vcr_state) -1; /* unknown */
}

static void
add_program_to_schedule		(const struct tm *	start_tm,
				 const struct tm *	end_tm,
				 const struct tm *	pdc_tm)
{
	struct program *p;
	struct program **pp;
	struct tm tm;
	time_t pil_time;

	/* Note PILs represent the originally announced start date of
	   the program in the time zone of the intended audience. When
	   we convert pdc_tm to a PIL we assume that zone is the same
	   as the system time zone (TZ environment variable), and
	   start_tm, end_tm and pdc_tm are also given relative to this
	   time zone. We do not consider the case where a program
	   straddles a daylight saving time discontinuity, e.g. starts
	   in the CET zone and ends in the CEST zone. */

	p = calloc (1, sizeof (*p));
	assert (NULL != p);

	tm = *start_tm;
	tm.tm_isdst = -1; /* unknown */
	p->start_time = mktime (&tm);
	if ((time_t) -1 == p->start_time) {
		fprintf (stderr, "Invalid start time.\n");
		exit (EXIT_FAILURE);
	}

	tm = *start_tm;
	tm.tm_isdst = -1; /* unknown */
	tm.tm_hour = end_tm->tm_hour;
	tm.tm_min = end_tm->tm_min;
	if (end_tm->tm_hour < start_tm->tm_hour) {
		/* mktime() should handle a 32nd. */
		++tm.tm_mday;
	}
	p->end_time = mktime (&tm);
	if ((time_t) -1 == p->end_time) {
		fprintf (stderr, "Invalid end time.\n");
		exit (EXIT_FAILURE);
	}

	tm = *start_tm;
	tm.tm_isdst = -1; /* unknown */
	tm.tm_hour = pdc_tm->tm_hour;
	tm.tm_min = pdc_tm->tm_min;
	if (pdc_tm->tm_hour >= start_tm->tm_hour + 12) {
		/* mktime() should handle a 0th. */
		--tm.tm_mday;
	} else if (pdc_tm->tm_hour + 12 < start_tm->tm_hour) {
		++tm.tm_mday;
	}

	/* Normalize day and month. */
	pil_time = mktime (&tm);
	if ((time_t) -1 == pil_time
	    || NULL == localtime_r (&pil_time, &tm)) {
		fprintf (stderr, "Cannot determine PIL month/day.\n");
		exit (EXIT_FAILURE);
	}

	p->pil = VBI_PIL (tm.tm_mon + 1, /* 1 ... 12 */
			  tm.tm_mday,
			  tm.tm_hour,
			  tm.tm_min);

	if (!vbi_pil_validity_window (&p->pil_valid_start,
				      &p->pil_valid_end,
				      p->pil,
				      p->start_time,
				      NULL /* system tz */)) {
		fprintf (stderr, "Cannot determine PIL validity.\n");
		exit (EXIT_FAILURE);
	}

	p->index = 0;
	for (pp = &schedule; NULL != *pp; pp = &(*pp)->next)
		++p->index;

	*pp = p;

	if (0) {
		printf ("Program %u start: ", p->index);
		print_time (p->start_time);
		printf ("End:              ");
		print_time (p->end_time);
		printf ("PIL:              ");
		print_time (pil_time);
		printf ("PIL valid from:   ");
		print_time (p->pil_valid_start);
		printf ("PIL valid until:  ");
		print_time (p->pil_valid_end);
	}
}

static void
usage				(FILE *			fp)
{
	fprintf (fp,
"Please specify the start time of a program in the format\n"
"YYYY-MM-DD HH:MM, the end time HH:MM and a VPS/PDC time HH:MM.\n");
}

static void
parse_args			(int			argc,
				 char **		argv)
{
	struct tm start_tm;
	struct tm end_tm;
	struct tm pdc_tm;

	dev_name = "/dev/vbi";

	for (;;) {
		int c;

		c = getopt (argc, argv, "d:ht");

		if (-1 == c)
			break;

		switch (c) {
		case 'd':
			dev_name = optarg;
			break;

		case 'h':
			usage (stdout);
			exit (EXIT_SUCCESS);

		case 't':
			test_mode = TRUE;
			break;

		default:
			usage (stderr);
			exit (EXIT_FAILURE);
		}
	}

	while (argc - optind >= 4) {
		memset (&start_tm, 0, sizeof (struct tm));
		if (NULL == strptime (argv[optind + 0], "%Y-%m-%d",
				      &start_tm))
			goto invalid;
		if (NULL == strptime (argv[optind + 1], "%H:%M",
				      &start_tm))
			goto invalid;

		memset (&end_tm, 0, sizeof (struct tm));
		if (NULL == strptime (argv[optind + 2], "%H:%M",
				      &end_tm))
			goto invalid;

		memset (&pdc_tm, 0, sizeof (struct tm));
		if (NULL == strptime (argv[optind + 3], "%H:%M",
				      &pdc_tm))
			goto invalid;

		add_program_to_schedule (&start_tm, &end_tm, &pdc_tm);

		optind += 4;
	}

	if (argc != optind)
		goto invalid;

	return;

 invalid:
	usage (stderr);
	exit (EXIT_FAILURE);
}

int
main				(int			argc,
				 char **		argv)
{
	vbi_bool success;

	setlocale (LC_ALL, "");

	parse_args (argc, argv);

	exit_code = EXIT_SUCCESS;

	dec = vbi_decoder_new ();
	assert (NULL != dec);

	success = vbi_event_handler_register (dec, VBI_EVENT_PROG_ID,
					      event_handler,
					      /* user_data */ NULL);
	assert (success);

	reset_state ();

	standby_loop ();

	vbi_decoder_delete (dec);

	while (NULL != schedule)
		remove_program_from_schedule (schedule);

	exit (exit_code);
}
