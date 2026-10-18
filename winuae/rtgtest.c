/* rtgtest - runs inside the Amiga that E-UAE emulates: opens screens on the
 * emulated graphics card (uaegfx), draws colour bars and a line of text on
 * each, says what it got.
 *
 *     rtgtest [SECONDS n]      each screen is held n seconds (default 10)
 *
 * Screens: 640x480 8-bit (the palette: pens through picasso96.c's CLUT), then
 * 800x600 24-bit (true colour: the BGRA byte-swapped path). From left to right
 * the bars are black, red, green, yellow, blue, magenta, cyan, white; the text
 * (a template blit, not a fill) sits on the white bar's left neighbour.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68020 -o share/guest/rtgtest rtgtest.c
 */
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

static int one_screen(unsigned long w, unsigned long h, unsigned long d, unsigned long secs)
{
    unsigned long id;
    struct NameInfo ni;
    struct Screen *s;
    LONG pens[8];
    char text[64];
    int i;

    printf("rtgtest: asking for %lux%lu, %lu bits\n", w, h, d);
    id = BestModeID(BIDTAG_NominalWidth, w, BIDTAG_NominalHeight, h,
                    BIDTAG_Depth, d, TAG_DONE);
    if (id == INVALID_ID) {
        printf("rtgtest: FAIL no mode\n");
        return 0;
    }
    ni.Name[0] = 0;
    GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id);
    printf("rtgtest: mode $%08lx \"%s\"\n", id, (char *)ni.Name);

    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, d,
                       SA_Title, (ULONG)"rtgtest", SA_ShowTitle, FALSE, SA_Quiet, TRUE, TAG_DONE);
    if (!s) {
        printf("rtgtest: FAIL OpenScreen\n");
        return 0;
    }
    printf("rtgtest: screen open %ux%u, depth %u\n", (unsigned)s->Width, (unsigned)s->Height,
           (unsigned)GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH));
    for (i = 0; i < 8; i++) {                   /* black, red, green, yellow, blue, magenta, cyan, white */
        ULONG r = (i & 1) ? 0xffffffff : 0, g = (i & 2) ? 0xffffffff : 0, b = (i & 4) ? 0xffffffff : 0;
        /* pens 0-7 set outright, on the true-colour screen too: with the 1999
           Picasso96, ObtainBestPen there handed back pens of the default
           palette (ee4444, eeeecc ...) */
        SetRGB32(&s->ViewPort, i, r, g, b);
        pens[i] = i;
        SetAPen(&s->RastPort, pens[i]);
        RectFill(&s->RastPort, i * s->Width / 8, 0, (i + 1) * s->Width / 8 - 1, s->Height - 1);
    }
    sprintf(text, "rtgtest %lux%lux%lu", w, h, d);
    SetAPen(&s->RastPort, pens[0]);
    SetBPen(&s->RastPort, pens[7]);
    SetDrMd(&s->RastPort, JAM2);
    SetAPen(&s->RastPort, pens[7]);             /* white on black, on the black bar */
    SetBPen(&s->RastPort, pens[0]);
    Move(&s->RastPort, 8, s->Height / 2);
    Text(&s->RastPort, (STRPTR)text, strlen(text));
    printf("rtgtest: bars and text drawn, holding %lu s\n", secs);
    fflush(stdout);
    Delay(secs * 50);
    CloseScreen(s);
    return 1;
}

/* every mode of the uaegfx card the display database has */
static void list_modes(void)
{
    ULONG id = INVALID_ID;
    int n = 0;
    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct NameInfo ni;
        struct DimensionInfo di;
        ni.Name[0] = 0;
        if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id) ||
            strncmp((char *)ni.Name, "uaegfx", 6))
            continue;
        GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof di, DTAG_DIMS, id);
        printf("rtgtest:   $%08lx %-28s nominal %dx%d, depth %u\n", (unsigned long)id, (char *)ni.Name,
               di.Nominal.MaxX - di.Nominal.MinX + 1, di.Nominal.MaxY - di.Nominal.MinY + 1,
               (unsigned)di.MaxDepth);
        n++;
    }
    printf("rtgtest: %d uaegfx modes\n", n);
}

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"SECONDS/N", args, NULL);
    unsigned long secs = args[0] ? *(LONG *)args[0] : 10;
    int ok;
    list_modes();
    ok = one_screen(640, 480, 8, secs);
    ok &= one_screen(800, 600, 24, secs);
    printf("rtgtest: %s\n", ok ? "OK" : "FAILED");
    if (rd) FreeArgs(rd);
    return ok ? 0 : 10;
}
