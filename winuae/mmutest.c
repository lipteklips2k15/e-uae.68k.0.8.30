/* mmutest: the 68030 MMU emulation the way a ROM remapper uses it
 * (winuae rig guest test; E-UAE with cpu_type=68030).
 *
 * Copies the 512K Kickstart into fast RAM with a marker at $FFFF00, maps
 * $F80000-$FFFFFF onto the copy write-protected (4K pages, everything
 * else identity), switches the MMU on and checks what the CPU sees.
 *
 *   m68k-amigaos-gcc -m68030 -O2 -noixemul -o mmutest mmutest.c
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>

#define MARK 0x5a5a1234UL
#define ROMOFF 0x7ff00          /* $FFFF00 in the ROM */

static ULONG crp[2] __attribute__((aligned(8)));
static ULONG tc_on, tc_off, tt_none;
/* SetPatch's MMU setup, put back afterwards */
static ULONG old_tc, old_tt0, old_tt1, old_crp[2] __attribute__((aligned(8)));
static volatile ULONG seen, after_write, seen_off;
static UWORD mmusr;
static ULONG *desc_a0;

int main (void)
{
    ULONG *tablea, *tableb, i, ssp;
    UBYTE *mem, *copy;
    int ok = 1;

    if (!(SysBase->AttnFlags & AFF_68030)) {
        printf ("mmutest: not a 68030\n");
        return 20;
    }
    /* the copy: 512K on a 4K boundary; tables on 16-byte boundaries */
    mem = AllocMem (0x80000 + 0x1000 + 256 * 4 + 4096 * 4 + 32, MEMF_FAST | MEMF_CLEAR);
    if (!mem) {
        printf ("mmutest: no fast RAM\n");
        return 20;
    }
    copy = (UBYTE *) (((ULONG) mem + 0xfff) & ~0xfff);
    tablea = (ULONG *) (((ULONG) copy + 0x80000 + 15) & ~15);
    tableb = tablea + 256;
    CopyMemQuick ((APTR) 0xf80000, copy, 0x80000);
    *(ULONG *) (copy + ROMOFF) = MARK;

    /* TC: E, PS 12 (4K), IS 0, TIA 8, TIB 12 */
    tc_on = 0x80000000 | (12 << 20) | (8 << 12) | (12 << 8);
    tc_off = 0;
    for (i = 0; i < 256; i++)           /* 16 MB each: themselves */
        tablea[i] = (i << 24) | 1;
    tablea[0] = (ULONG) tableb | 2;     /* the first 16 MB: in 4K pages */
    for (i = 0; i < 4096; i++)
        tableb[i] = (i << 12) | 1;
    for (i = 0; i < 128; i++)           /* $F80000-$FFFFFF: the copy, write-protected */
        tableb[0xf80 + i] = ((ULONG) copy + (i << 12)) | 4 | 1;
    crp[0] = 0x7fff0002;                /* no limit, short descriptors */
    crp[1] = (ULONG) tablea;

    Disable ();
    ssp = (ULONG) SuperState ();
    __asm__ volatile ("pmove %%tc,%0" : "=m" (old_tc));
    __asm__ volatile ("pmove %%crp,%0" : "=m" (old_crp[0]));
    __asm__ volatile ("pmove %%tt0,%0" : "=m" (old_tt0));
    __asm__ volatile ("pmove %%tt1,%0" : "=m" (old_tt1));
    /* transparent translation (SetPatch's) would override the tables */
    __asm__ volatile ("pmove %0,%%tt0" : : "m" (tt_none));
    __asm__ volatile ("pmove %0,%%tt1" : : "m" (tt_none));
    __asm__ volatile ("pmove %0,%%crp" : : "m" (crp[0]));
    __asm__ volatile ("pmove %0,%%tc" : : "m" (tc_on));
    seen = *(volatile ULONG *) 0xffff00;
    *(volatile ULONG *) 0xffff00 = 0x11111111;
    after_write = *(volatile ULONG *) 0xffff00;
    __asm__ volatile ("move.l #0xffff00,%%a1\n\t"
                      "ptestr #5,(%%a1),#7,%%a0\n\t"
                      "pmove %%psr,%0\n\t"
                      "move.l %%a0,%1"
                      : "=m" (mmusr), "=m" (desc_a0) : : "a0", "a1");
    __asm__ volatile ("pmove %0,%%tc" : : "m" (tc_off));
    seen_off = *(volatile ULONG *) 0xffff00;
    __asm__ volatile ("pmove %0,%%crp" : : "m" (old_crp[0]));
    __asm__ volatile ("pmove %0,%%tt0" : : "m" (old_tt0));
    __asm__ volatile ("pmove %0,%%tt1" : : "m" (old_tt1));
    __asm__ volatile ("pmove %0,%%tc" : : "m" (old_tc));
    UserState ((APTR) ssp);
    Enable ();

    printf ("mmutest: MMU on, $FFFF00 = %08lx (want %08lx, the copy)\n", seen, MARK);
    ok &= seen == MARK;
    printf ("mmutest: after a write to it = %08lx (want %08lx, write-protected)\n", after_write, MARK);
    ok &= after_write == MARK;
    printf ("mmutest: PTEST MMUSR = %04x (want levels 2, no I/W... W set: %s), descriptor at %08lx (want %08lx)\n",
            mmusr, (mmusr & 0x0800) ? "yes" : "no", (ULONG) desc_a0, (ULONG) &tableb[0xfff]);
    ok &= (mmusr & 0x0407) == 2 && (mmusr & 0x0800) && desc_a0 == &tableb[0xfff];
    printf ("mmutest: MMU off, $FFFF00 = %08lx (want the ROM's, not %08lx)\n", seen_off, MARK);
    ok &= seen_off != MARK;
    printf ("mmutest: %s\n", ok ? "ALL OK" : "FAILED");
    FreeMem (mem, 0x80000 + 0x1000 + 256 * 4 + 4096 * 4 + 32);
    return ok ? 0 : 10;
}
