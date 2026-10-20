/* Future Retro Fusion */
#ifndef EVENTS_H
#define EVENTS_H

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Events
  * These are best for low-frequency events. Having too many of them,
  * or using them for events that occur too frequently, can cause massive
  * slowdown.
  *
  * Copyright 1995-1998 Bernd Schmidt
  */

#undef EVENT_DEBUG

#include "machdep/rpt.h"
#include "hrtimer.h"

/* Every Amiga hardware clock cycle takes this many "virtual" cycles.  This
 * used to be hardcoded as 1, but using higher values allows us to time some
 * stuff more precisely.
 * 512 is the official value from now on - it can't change, unless we want
 * _another_ config option "finegrain2_m68k_speed".
 *
 * We define this value here rather than in events.h so that gencpu.c sees
 * it.
 */
#define CYCLE_UNIT 512

/* This one is used by cfgfile.c.  We could reduce the CYCLE_UNIT back to 1,
 * I'm not 100% sure this code is bug free yet.
 */
#define OFFICIAL_CYCLE_UNIT 512

extern volatile frame_time_t vsynctime, vsyncmintime;
extern void reset_frame_rate_hack (void);
extern int rpt_available;
extern frame_time_t syncbase;

extern void compute_vsynctime (void);
extern void do_cycles_ce (long cycles);


extern unsigned long currcycle;
extern unsigned int is_lastline;
extern int lastline_time_every;         /* custom.c, from prefs cpu_timecheck */
extern void custom_set_timecheck (int n);

extern unsigned long nextevent;

typedef void (*evfunc)(void);

struct ev
{
    int active;
    unsigned long int evtime, oldcycles;
    evfunc handler;
};

enum {
    ev_hsync, ev_copper, ev_audio, ev_cia, ev_blitter, ev_disk,
    ev_max
};

extern struct ev eventtab[ev_max];

extern void init_eventtab (void);
extern void events_schedule (void);
extern void handle_active_events (void);

#ifdef JIT
/* For faster cycles handling */
extern signed long pissoff;
#endif

/*
 * Handle all events pending within the next cycles_to_add cycles
 */
STATIC_INLINE void do_cycles (unsigned int cycles_to_add)
{
#ifdef JIT
    if ((pissoff -= cycles_to_add) >= 0)
	return;

    cycles_to_add = -pissoff;
    pissoff = 0;
#endif

    if (is_lastline && eventtab[ev_hsync].evtime - currcycle <= cycles_to_add) {
	/* The CPU's spare time at the end of a frame, until the frame's real
	 * time is up. Reading the time is costly (AmigaOS: ReadEClock, CIA
	 * reads - over the bus on a PiStorm), and was done for every
	 * instruction here: look only every lastline_time_every-th time
	 * (prefs cpu_timecheck). */
	static int time_skip;
	frame_time_t rpt, v;
	if (time_skip-- > 0)
	    return;
	time_skip = lastline_time_every;
	rpt = uae_gethrtime ();
	v   = rpt - vsyncmintime;
	if (v > syncbase || v < -(syncbase))
	    vsyncmintime = rpt;
	if (v < 0) {
#ifdef JIT
            pissoff = 3000 * CYCLE_UNIT;
#endif
	    return;
	}
	time_skip = 0;          /* time's up: the next frame looks at once */
    }

    while ((nextevent - currcycle) <= cycles_to_add) {
	int i;
	cycles_to_add -= (nextevent - currcycle);
	currcycle = nextevent;

	for (i = 0; i < ev_max; i++) {
	     if (eventtab[i].active && eventtab[i].evtime == currcycle)
		  (*eventtab[i].handler)();
	}
	events_schedule ();
    }

    currcycle += cycles_to_add;
}

STATIC_INLINE unsigned long get_cycles (void)
{
    return currcycle;
}

STATIC_INLINE void set_cycles (unsigned long x)
{
#ifdef JIT
    currcycle = x;
#endif
}

STATIC_INLINE void cycles_do_special (void)
{
#ifdef JIT
    if (pissoff >= 0)
        pissoff = -1;
#endif
}

STATIC_INLINE void do_extra_cycles (unsigned long cycles_to_add)
{
#ifdef JIT
    pissoff -= cycles_to_add;
#endif
}

#define countdown pissoff

#endif
