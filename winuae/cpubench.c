/* cpubench - how fast E-UAE's emulated CPU runs, timed in the guest
 * (timer.device E-clock): memory reads in fast RAM, memory writes into chip
 * RAM, and a register-only loop as the control.
 *   cpubench [label]
 */
#include <stdio.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

struct Device *TimerBase;
static struct timerequest tr;
static ULONG efreq;

static ULONG now (void)        /* E-clock ticks (wraps after ~100 minutes) */
{
    struct EClockVal e;
    efreq = ReadEClock (&e);
    return e.ev_lo;
}

static ULONG ms (ULONG ticks)
{
    return ticks / (efreq / 1000);
}

#define SIZE (256 * 1024)

int main (int argc, char **argv)
{
    ULONG *fast = AllocMem (SIZE, MEMF_FAST), *chip = AllocMem (SIZE, MEMF_CHIP);
    volatile ULONG sum = 0;
    ULONG t0, t1, t2, t3;
    int pass, i;

    if (OpenDevice ((STRPTR) TIMERNAME, UNIT_ECLOCK, (struct IORequest *) &tr, 0) || !fast || !chip) {
        printf ("cpubench: setup failed\n");
        return 20;
    }
    TimerBase = tr.tr_node.io_Device;
    for (i = 0; i < SIZE / 4; i++)
        fast[i] = i * 2654435761u;

    t0 = now ();
    for (pass = 0; pass < 40; pass++) {             /* 40 x 64K long reads */
        ULONG s = 0, *p = fast;
        for (i = 0; i < SIZE / 4; i += 4)
            s += p[i] ^ p[i + 1] ^ p[i + 2] ^ p[i + 3];
        sum += s;
    }
    t1 = now ();
    for (pass = 0; pass < 40; pass++) {             /* 40 x 64K long writes */
        ULONG *p = chip, v = pass;
        for (i = 0; i < SIZE / 4; i += 4) {
            p[i] = v; p[i + 1] = v; p[i + 2] = v; p[i + 3] = v;
        }
    }
    t2 = now ();
    {                                               /* registers only */
        ULONG a = 1, b = 3;
        for (i = 0; i < 2000000; i++) {
            a = a * 5 + b;
            b ^= a >> 3;
        }
        sum += a + b;
    }
    t3 = now ();
    printf ("cpubench %s: fast-RAM reads %lu ms, chip writes %lu ms, registers %lu ms\n",
            argc > 1 ? argv[1] : "", ms (t1 - t0), ms (t2 - t1), ms (t3 - t2));
    CloseDevice ((struct IORequest *) &tr);
    FreeMem (fast, SIZE);
    FreeMem (chip, SIZE);
    return 0;
}
