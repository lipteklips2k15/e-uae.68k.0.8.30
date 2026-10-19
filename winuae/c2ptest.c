/* c2ptest - checks the emulated Akiko C2P (E-UAE akiko_c2p=true) from
 * inside the guest: the ID, Kickstart's GfxBase->ChunkyToPlanarPtr, and
 * converted blocks against a software C2P. Prints to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>

struct GfxBase *GfxBase;

static void soft_c2p (const ULONG *chunky, ULONG *planar)
{
    int n, p;
    for (p = 0; p < 8; p++)
        planar[p] = 0;
    for (n = 0; n < 32; n++) {
        UBYTE pix = chunky[n / 4] >> (24 - 8 * (n % 4));
        for (p = 0; p < 8; p++)
            if (pix & (1 << p))
                planar[p] |= 1UL << (31 - n);
    }
}

int main (int argc, char **argv)
{
    int wcp_only = argc > 1;    /* c2ptest WCP: only graphics.library's route */
    volatile UWORD *id = (volatile UWORD *) 0xb80002;
    volatile ULONG *c2p = (volatile ULONG *) 0xb80038;
    ULONG chunky[8], want[8], got[8];
    int t, i, bad = 0;

    GfxBase = (struct GfxBase *) OpenLibrary ("graphics.library", 39);
    if (!GfxBase)
        return 20;
    printf ("c2ptest: Akiko ID $%04x (%s)\n", *id, *id == 0xcafe ? "found" : "none");
    printf ("c2ptest: GfxBase->ChunkyToPlanarPtr = $%08lx\n", (unsigned long) GfxBase->ChunkyToPlanarPtr);
    if (*id != 0xcafe)
        return 5;
    srand (1234);
    for (t = 0; t < (wcp_only ? 0 : 200); t++) {
        for (i = 0; i < 8; i++)
            chunky[i] = ((ULONG) rand () << 16) ^ (ULONG) rand ();
        if (t == 0)
            for (i = 0; i < 8; i++)
                chunky[i] = 0x00010203 + 0x04040404 * i;       /* pixels 0..31 */
        soft_c2p (chunky, want);
        for (i = 0; i < 8; i++)
            *c2p = chunky[i];
        for (i = 0; i < 8; i++)
            got[i] = *c2p;
        for (i = 0; i < 8; i++)
            if (got[i] != want[i]) {
                if (bad++ < 4)
                    printf ("c2ptest: block %d plane %d: got $%08lx want $%08lx\n", t, i,
                            (unsigned long) got[i], (unsigned long) want[i]);
            }
    }
    printf ("c2ptest: 200 blocks, %d wrong planes - %s\n", bad, bad ? "FAIL" : "OK");

    /* graphics.library's route: WriteChunkyPixels into a planar 8-bit
       bitmap (through Akiko when ChunkyToPlanarPtr is set), read back */
    {
        struct BitMap *bm = AllocBitMap (64, 8, 8, BMF_CLEAR, NULL);
        struct RastPort rp;
        UBYTE src[64 * 8];
        int x, y, wrong = 0;
        if (bm) {
            InitRastPort (&rp);
            rp.BitMap = bm;
            for (i = 0; i < 64 * 8; i++)
                src[i] = (UBYTE) (i * 37 + 11);
            WriteChunkyPixels (&rp, 0, 0, 63, 7, src, 64);
            for (y = 0; y < 8; y++)
                for (x = 0; x < 64; x++)
                    if ((UBYTE) ReadPixel (&rp, x, y) != src[y * 64 + x])
                        wrong++;
            printf ("c2ptest: WriteChunkyPixels 64x8x8 (bitmap %s): %d wrong pixels - %s\n",
                    GetBitMapAttr (bm, BMA_FLAGS) & BMF_STANDARD ? "planar" : "other",
                    wrong, wrong ? "FAIL" : "OK");
            FreeBitMap (bm);
            bad += wrong;
        }
    }
    return bad ? 10 : 0;
}
