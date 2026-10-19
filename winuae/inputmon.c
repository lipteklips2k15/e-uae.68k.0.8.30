/* inputmon - logs what the emulated Amiga's button lines show: CIA-A PRA
 * bits 6/7 (port 1/2 fire, i.e. the left mouse button) and POTGOR (right
 * and middle buttons, CD32 pad serial data). Runs in the E-UAE guest.
 *
 *   inputmon [seconds] [logfile]     default: 180 SYS:inputmon.log
 */
#include <stdio.h>
#include <stdlib.h>
#include <proto/dos.h>

int main (int argc, char **argv)
{
    volatile unsigned char  *pra    = (volatile unsigned char *) 0xbfe001;
    volatile unsigned char  *ddra   = (volatile unsigned char *) 0xbfe201;
    volatile unsigned short *potgor = (volatile unsigned short *) 0xdff016;
    int secs = argc > 1 ? atoi (argv[1]) : 180;
    const char *name = argc > 2 ? argv[2] : "SYS:inputmon.log";
    unsigned int last_a = 0xffff, last_p = 0xffff, last_d = 0xffff;
    long tick, ticks = (long) secs * 50;
    FILE *f = fopen (name, "w");

    if (!f)
        return 20;
    fprintf (f, "inputmon: %d s; PRA bit6 = port1 fire/LMB, bit7 = port2 fire (0 = pressed)\n", secs);
    fprintf (f, "POTGOR 0x0400 = port1 RMB, 0x0100 = port1 MMB, 0x4000/0x1000 = port2 (0 = pressed)\n");
    fflush (f);
    for (tick = 0; tick < ticks; tick++) {
        unsigned int a = *pra & 0xc0, p = *potgor & 0x5500, d = *ddra & 0xc0;
        if (a != last_a || p != last_p || d != last_d) {
            fprintf (f, "%4ld.%02ld s  PRA %02x  DDRA %02x  POTGOR %04x  LMB %s  RMB %s  MMB %s  port2 fire %s\n",
                     tick / 50, (tick % 50) * 2, a, d, p,
                     (a & 0x40) ? "up" : "DOWN", (p & 0x0400) ? "up" : "DOWN",
                     (p & 0x0100) ? "up" : "DOWN", (a & 0x80) ? "up" : "DOWN");
            fflush (f);
            last_a = a;
            last_p = p;
            last_d = d;
        }
        Delay (1);
    }
    fprintf (f, "inputmon: done\n");
    fclose (f);
    return 0;
}
