/* SetAkiko - tells graphics.library about an Akiko C2P converter.
 *
 * Kickstart fills in GfxBase->ChunkyToPlanarPtr only on a CD32. With E-UAE's
 * akiko_c2p=true (Settings: "Akiko C2P") an emulated A1200 has the
 * converter too; run this early in the Startup-Sequence and
 * WriteChunkyPixels() uses it for planar bitmaps.
 *
 *   SetAkiko          set the pointer if Akiko answers ($C0CACAFE)
 *   SetAkiko OFF      clear it again
 *
 * Build: m68k-amigaos-gcc -mcrt=clib2 -O2 -o SetAkiko SetAkiko.c
 */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>

struct GfxBase *GfxBase;

int main (int argc, char **argv)
{
    volatile ULONG *id = (volatile ULONG *) 0xb80000;
    int off = argc > 1 && !strcasecmp (argv[1], "OFF");

    GfxBase = (struct GfxBase *) OpenLibrary ("graphics.library", 40);
    if (!GfxBase) {
        printf ("SetAkiko: needs graphics.library V40 (OS 3.1)\n");
        return 20;
    }
    if (off) {
        GfxBase->ChunkyToPlanarPtr = 0;
        printf ("SetAkiko: off\n");
    } else if (*id != 0xc0cacafe) {
        printf ("SetAkiko: no Akiko here (ID $%08lx)\n", (unsigned long) *id);
        CloseLibrary ((struct Library *) GfxBase);
        return 5;
    } else {
        GfxBase->ChunkyToPlanarPtr = (ULONG *) 0xb80038;
        printf ("SetAkiko: Akiko C2P at $B80038 for graphics.library\n");
    }
    CloseLibrary ((struct Library *) GfxBase);
    return 0;
}
