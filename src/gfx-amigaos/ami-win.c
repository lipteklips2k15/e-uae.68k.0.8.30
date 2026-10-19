#warning "HOTKEYS_PATCH_P2FREEZE active in gfx-amigaos/ami-win.c" /* HOTKEYS_PATCH_P2FREEZE */
/*
  * UAE - The Un*x Amiga Emulator
  *
  * Amiga interface
  *
  * Copyright 1996,1997,1998 Samuel Devulder.
  * Copyright 2003-2007 Richard Drummond
  * Copyright 2025-26 Future Retro Fusion
  */
/* --- required include order: types first, prefs next, then gui --- */
#include "gui.h"
#include "sysconfig.h"
#include "sysdeps.h"

 /* struct uae_prefs */

#include "savestate.h"  /* for savestate_quick(), savestate_initsave() */
/* Fallbacks if the header above doesn't provide them */
#ifndef RAWKEY_NP_7
# define RAWKEY_NP_7   0x3D
#endif
#ifndef RAWKEY_NP_8
# define RAWKEY_NP_8   0x3E
#endif
#ifndef RAWKEY_NP_ADD
# define RAWKEY_NP_ADD 0x5E  /* keypad '+' (Amiga TRM) */
#endif
#ifndef RAWKEY_NP_SUB
# define RAWKEY_NP_SUB 0x4A  /* keypad '-' (common mapping) */
#endif
/* Qualifiers: some headers don't provide IEQUALIFIER_COMMAND */
#ifndef IEQUALIFIER_LCOMMAND
# define IEQUALIFIER_LCOMMAND 0
#endif
#ifndef IEQUALIFIER_RCOMMAND
# define IEQUALIFIER_RCOMMAND 0
#endif
/* Local-only hotkey events for quick save/restore */
#ifndef INPUTEVENT_SPC_STATESAVEQUICK
# define INPUTEVENT_SPC_STATESAVEQUICK    0x7F01
#endif
#ifndef INPUTEVENT_SPC_STATERESTOREQUICK
# define INPUTEVENT_SPC_STATERESTOREQUICK 0x7F02
#endif
/* Some trees don't expose INPUTEVENT_NONE to this TU; treat 0 as "none". */
#ifndef INPUTEVENT_NONE
#define INPUTEVENT_NONE 0
#endif


/* sam: Argg!! Why did phase5 change the path to cybergraphics ? */
//#define CGX_CGX_H <cybergraphics/cybergraphics.h>

#ifdef HAVE_LIBRARIES_CYBERGRAPHICS_H
# define CGX_CGX_H <libraries/cybergraphics.h>
# define USE_CYBERGFX           /* define this to have cybergraphics support */
#else
# ifdef HAVE_CYBERGRAPHX_CYBERGRAPHICS_H
#  define USE_CYBERGFX
#  define CGX_CGX_H <cybergraphx/cybergraphics.h>
# endif
#endif
#ifdef USE_CYBERGFX
# if defined __MORPHOS__ || defined __AROS__ || defined __amigaos4__
#  define USE_CYBERGFX_V41
# endif
#endif

//#define DEBUG

/****************************************************************************/

#include <exec/execbase.h>
#include <exec/memory.h>

#include <dos/dos.h>
#include <dos/dosextens.h>

#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>

#include <libraries/asl.h>
#include <intuition/pointerclass.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>

/****************************************************************************/

# ifdef __amigaos4__
#  define __USE_BASETYPE__
# endif
# include <proto/intuition.h>
# include <proto/graphics.h>
# include <proto/layers.h>
# include <proto/exec.h>
# include <proto/dos.h>
# include <proto/asl.h>

#ifdef USE_CYBERGFX
# ifdef __SASC
#  include CGX_CGX_H
#  include <proto/cybergraphics.h>
# else /* not SAS/C => gcc */
#  include CGX_CGX_H
#  include <proto/cybergraphics.h>
# endif
# ifndef BMF_SPECIALFMT
#  define BMF_SPECIALFMT 0x80	/* should be cybergraphics.h but isn't for  */
				/* some strange reason */
# endif
#  ifndef RECTFMT_RAW
#   define RECTFMT_RAW     5
#  endif
#endif /* USE_CYBERGFX */

/****************************************************************************/

#include <ctype.h>
#include <signal.h>

/****************************************************************************/

#include "uae.h"
#include "options.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"
#include "inputdevice.h"
#include "keyboard.h"
#include "keybuf.h"
#include "audio.h"   /* FRF: for audio_volume() */
#include "debug.h"
#include "hotkeys.h"
#include "version.h"

#define BitMap Picasso96BitMap  /* Argh! */
#include "picasso96.h"
#undef BitMap
void ami_pause_loop(void);
/* FRF: after quick save/load, ignore hotkeys until Ctrl/LAlt released */
static volatile int frf_block_hotkeys_until_release = 0;

/* Weak stub for activate_debugger.
 * ami-rexx.o references this unconditionally (compiled with DEBUGGER),
 * but debugger.c is not in this build.  The weak attribute means the
 * real symbol will silently win if debugger.c is ever linked back in. */
void __attribute__((weak)) activate_debugger (void) {}

#ifdef DEBUGGER
#endif
extern void frf_volume_set_percent(int percent);
/* FRF: volume setter used by hotkey_adjust_volume()
 * Keep it in this TU so ami-win.o always links.
 */
void frf_volume_set_percent(int percent)
{
    /* Clamp 0..100 */
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    /* UAE prefs: keep both in sync so it survives & applies consistently */
    currprefs.sound_volume   = percent;
    changed_prefs.sound_volume = percent;

    /* Optional: log so you can see it working */
    write_log("VOLUME: %d%%\n", percent);

    /* If your tree has a dedicated apply hook, you can call it here.
     *       Leave this as-is unless you KNOW the function name exists. */
    /* sound_prefs_changed(); */
}

/* ARexx interface (ami-rexx.c) */
extern void rexx_init (void);
extern void rexx_exit (void);
extern void rexx_handle_events (void);
/* FRF 2025: 3-line startup splash on Workbench, auto-closes after ~3 seconds */

/* IntuitionBase is already declared globally elsewhere in ami-win.c */
extern struct IntuitionBase *IntuitionBase;

/* Modified by Future Retro Fusion on 2026-03-20.
 * This file remains licensed under the GNU General Public License,
 * version 2 or (at your option) any later version.
 */

/* Modified by Future Retro Fusion on 2026-03-20.
 * This file remains licensed under the GNU General Public License,
 * version 2 or (at your option) any later version.
 */

/*
 * ami_draw_dot — draw one lit dot of the dot-matrix logo.
 * ox,oy = top-left origin of the character cell.
 * col,row = 0-based column and row within the 5x7 grid.
 * DOT_S = side length of each square dot in pixels.
 * COL_P = column pitch (dot centre-to-centre horizontally).
 * ROW_P = row pitch (dot centre-to-centre vertically).
 */
#define DOT_S  4    /* dot square size  */
#define COL_P  9    /* column pitch px  */
#define ROW_P 10    /* row pitch px     */

static void ami_draw_dot(struct RastPort *rp, WORD ox, WORD oy, int col, int row)
{
    WORD x0 = ox + col * COL_P;
    WORD y0 = oy + row * ROW_P;
    RectFill(rp, x0, y0, x0 + DOT_S - 1, y0 + DOT_S - 1);
}

/*
 * ami_draw_dm_char — render one 5x7 dot-matrix character.
 * bitmap[7] holds the 7 rows, each row is a 5-bit mask (bit4=leftmost col).
 */
static void ami_draw_dm_char(struct RastPort *rp, WORD ox, WORD oy,
                              const UBYTE bitmap[7])
{
    int row, col;
    for (row = 0; row < 7; row++) {
        UBYTE bits = bitmap[row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col))
                ami_draw_dot(rp, ox, oy, col, row);
        }
    }
}

/*
 * ami_draw_startup_splash — dot-matrix "E-UAE" logo + text credits.
 *
 * Logo metrics (pixels):
 *   dot size   : DOT_S x DOT_S
 *   col pitch  : COL_P   (between dot origins within a char)
 *   row pitch  : ROW_P
 *   char width : 5 * COL_P = 45 px
 *   char gap   : 14 px    (space between characters)
 *   char pitch : 45 + 14  = 59 px
 *   5 chars    : 4 * 59 + 45 = 281 px total logo width
 *   logo height: 6 * ROW_P + DOT_S = 64 px
 */
static void ami_draw_startup_splash(struct Window *win)
{
    struct RastPort *rp;
    struct TextFont *oldFont = NULL;
    struct TextFont *newFont = NULL;
    struct TextAttr  ta;

    /* --- 5x7 dot-matrix bitmaps (bit4 = leftmost column) ---
     *
     *  E : 11111 / 10000 / 10000 / 11110 / 10000 / 10000 / 11111
     *  - : 00000 / 00000 / 00000 / 01110 / 00000 / 00000 / 00000
     *  U : 10001 / 10001 / 10001 / 10001 / 10001 / 10001 / 01110
     *  A : 01110 / 10001 / 10001 / 11111 / 10001 / 10001 / 10001
     *  E : 11111 / 10000 / 10000 / 11110 / 10000 / 10000 / 11111
     */
    static const UBYTE dm_E[7] = {
        0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F
    };
    static const UBYTE dm_DASH[7] = {
        0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00
    };
    static const UBYTE dm_U[7] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E
    };
    static const UBYTE dm_A[7] = {
        0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11
    };

    /* pointers in order: E  -  U  A  E */
    static const UBYTE * const dm_chars[5] = {
        dm_E, dm_DASH, dm_U, dm_A, dm_E
    };

    /* --- credit / info lines (topaz 6) --- */
    const STRPTR l1 = (STRPTR)"E-UAE 68k 0.8.30 2026.64b";
    const STRPTR l2 = (STRPTR)"   ";
    const STRPTR l3 = (STRPTR)"(C)1995-2007 Richard Drummond et al.";
    const STRPTR l4 = (STRPTR)"Mods (C)2025-2026 Future Retro Fusion";
    const STRPTR l5 = (STRPTR)"GPL v2+ ABSOLUTELY NO WARRANTY";
    const STRPTR l6 = (STRPTR)"Redistributable GNU GPL";

    LONG  innerWidth, innerHeight;
    WORD  lineH;
    WORD  logoW, logoH;
    WORD  totalH, startY;
    WORD  logoX, logoY;
    WORD  textY, x;
    int   gap, i;

    if (!win) return;
    rp = win->RPort;
    if (!rp) return;

    /* topaz 6 for the credit lines */
    ta.ta_Name  = (STRPTR)"topaz.font";
    ta.ta_YSize = 6;
    ta.ta_Style = FS_NORMAL;
    ta.ta_Flags = 0;

    oldFont = rp->Font;
    newFont = OpenFont(&ta);
    if (newFont) SetFont(rp, newFont);

    innerWidth  = win->Width  - win->BorderLeft - win->BorderRight;
    innerHeight = win->Height - win->BorderTop  - win->BorderBottom;
    lineH = (rp->TxHeight > 0) ? rp->TxHeight : 6;

    /* logo pixel dimensions */
    gap   = 14;                          /* px between chars          */
    logoW = 4 * (5 * COL_P + gap) + 5 * COL_P;   /* = 4*59 + 45 = 281 px */
    logoH = 6 * ROW_P + DOT_S;          /* = 64 px                   */

    /*
     * Vertical layout:
     *   logoH  (dot-matrix logo)
     *   8 px   gap between logo and text
     *   6 * lineH  (six credit lines)
     */
    totalH = logoH + 8 + 6 * lineH;
    startY = win->BorderTop + (innerHeight - totalH) / 2;

    /* --- Clear --- */
    SetAPen(rp, 0);
    RectFill(rp,
             win->BorderLeft,  win->BorderTop,
             win->Width  - 1 - win->BorderRight,
             win->Height - 1 - win->BorderBottom);

    /* --- Outline box --- */
    SetAPen(rp, 1);
    Move(rp, win->BorderLeft,  win->BorderTop);
    Draw(rp, win->Width  - 1 - win->BorderRight, win->BorderTop);
    Draw(rp, win->Width  - 1 - win->BorderRight, win->Height - 1 - win->BorderBottom);
    Draw(rp, win->BorderLeft,  win->Height - 1 - win->BorderBottom);
    Draw(rp, win->BorderLeft,  win->BorderTop);

    /* --- Dot-matrix logo --- */
    logoX = win->BorderLeft + (innerWidth - logoW) / 2;
    logoY = startY;

    SetAPen(rp, 1);
    for (i = 0; i < 5; i++) {
        WORD cx = logoX + i * (5 * COL_P + gap);
        ami_draw_dm_char(rp, cx, logoY, dm_chars[i]);
    }

    /* --- Credit lines --- */
    textY = startY + logoH + 8;

#define CREDIT(str, off) \
    x = win->BorderLeft + (innerWidth - TextLength(rp, (str), strlen((char *)(str)))) / 2; \
    Move(rp, x, textY + rp->TxBaseline + (off) * lineH); \
    Text(rp, (str), strlen((char *)(str)));

    CREDIT(l1, 0)
    CREDIT(l2, 1)
    CREDIT(l3, 2)
    CREDIT(l4, 3)
    CREDIT(l5, 4)
    CREDIT(l6, 5)

#undef CREDIT

    if (oldFont) SetFont(rp, oldFont);
    if (newFont) CloseFont(newFont);
}

#undef DOT_S
#undef COL_P
#undef ROW_P

static void ami_show_startup_splash(void)
{
    struct Screen *scr;
    struct Window *win;
    ULONG ticks = 5 * 50; /* 5 seconds */

    if (!IntuitionBase)
        return;

    scr = LockPubScreen(NULL);
    if (!scr)
        return;

    /* Centered, borderless popup box */
    {
        WORD width  = 340;
        WORD height = 160;   /* dot-matrix logo (64px) + 8px gap + 6 credit lines */
        WORD left   = (scr->Width  - width)  / 2;
        WORD top    = (scr->Height - height) / 2;

        win = OpenWindowTags(NULL,
                             WA_CustomScreen,  (ULONG)scr,
                             WA_Left,          left,
                             WA_Top,           top,
                             WA_Width,         width,
                             WA_Height,        height,
                             WA_Borderless,    TRUE,
                             WA_Backdrop,      FALSE,
                             WA_Activate,      TRUE,
                             WA_DragBar,       FALSE,
                             WA_DepthGadget,   FALSE,
                             WA_CloseGadget,   FALSE,
                             WA_SizeGadget,    FALSE,
                             WA_SimpleRefresh, TRUE,
                             WA_IDCMP,         IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW,
                             TAG_DONE);
    }

    if (!win) {
        UnlockPubScreen(NULL, scr);
        return;
    }

    ami_draw_startup_splash(win);

    while (ticks > 0) {
        struct IntuiMessage *msg;

        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls   = msg->Class;
            UWORD mcode = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cls == IDCMP_MOUSEBUTTONS) {
                if (mcode == SELECTDOWN || mcode == MENUDOWN || mcode == MIDDLEDOWN) {
                    ticks = 0; /* click dismiss */
                    break;
                }
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                ami_draw_startup_splash(win);
                EndRefresh(win, TRUE);
            }
        }

        if (ticks == 0)
            break;

        Delay(5);
        ticks = (ticks > 5) ? (ticks - 5) : 0;
    }

    CloseWindow(win);
    UnlockPubScreen(NULL, scr);
}




/* === FRF: unique public screen name so multiple fullscreens can coexist === */

static char uae_pubname[32];

static const char *get_uae_pubname(void)
{
    if (!uae_pubname[0]) {
        /* Use the Task pointer as a simple per-process unique id */
        struct Task *t = FindTask(NULL);
        unsigned long id = (unsigned long)t;

        /* Result: "68kEUAE_1234ABCD" (fits easily in 32 bytes) */
        sprintf(uae_pubname, "68kEUAE_%08lx", id);
        write_log("AMIGFX: using public screen name '%s'\n", uae_pubname);
    }
    return uae_pubname;
}


/****************************************************************************/
/* ----- FRF: HUD + Volume hotkey helpers (Hotkeys + H/7/8) ----- */

static void hotkey_toggle_hud(void)
{
    /* HUD == status line in drawing.c, controlled by leds_on_screen */
    int new_on = !currprefs.leds_on_screen;

    currprefs.leds_on_screen     = new_on;
    changed_prefs.leds_on_screen = new_on;

    write_log("HOTKEY: HUD %s (leds_on_screen=%d)\n",
              new_on ? "ON" : "OFF", new_on);

    /* Force a clean redraw so the HUD appears/disappears immediately */
    notice_screen_contents_lost();
}
extern void frf_volume_set_percent(int percent);
static void hotkey_adjust_frameskip(int dir)
{
    /* dir: -1 = less skipping, +1 = more skipping */
    int fs_old = currprefs.gfx_framerate;
    int fs_new = fs_old + (dir < 0 ? -1 : +1);

    if (fs_new < 0)  fs_new = 0;
    if (fs_new > 20) fs_new = 20;

    if (fs_new == fs_old)
        return;

    currprefs.gfx_framerate     = fs_new;
    changed_prefs.gfx_framerate = fs_new;

    gui_frameskip_set(fs_new);
    gui_frameskip_flash(dir);

    write_log("HOTKEY: frameskip -> %d\n", fs_new);
}

static void hotkey_adjust_volume(int dir)
{
    /* dir: -1 = down, +1 = up */
    const int step = 5; /* % per press */
    int v = (changed_prefs.sound_volume > 0 ? changed_prefs.sound_volume
    : currprefs.sound_volume);

    v += (dir < 0 ? -step : step);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;

    /* Update prefs for UI/save; apply soft volume immediately (no reinit). */
    currprefs.sound_volume      = v;
    changed_prefs.sound_volume  = v;
    frf_volume_set_percent(v);

    write_log("HOTKEY: volume %s -> %d%% (softvol)\n",
              (dir < 0 ? "down" : "up"), v);
}



#define UAEIFF "UAEIFF"        /* env: var to trigger iff dump */
#define UAESM  "UAESM"         /* env: var for screen mode */
/* Quick state requests are executed inside the pause loop for safety */
static volatile int pending_quick_save = 0;
static volatile int pending_quick_load = 0;

/* Filename buffer for GUI-requested save/load.
 * Set by gui_request_save/load(), consumed by ami_pause_loop(). */
static char gui_state_filename[512] = "";

/* Called from ami-gui.c to safely schedule a state save.
 * The actual savestate_quick() runs inside ami_pause_loop() on the next
 * tick, never mid-frame, which avoids crashes from reentrancy. */
void gui_request_save (const char *path)
{
    if (!path || !path[0])
        return;
    strncpy (gui_state_filename, path, sizeof (gui_state_filename) - 1);
    gui_state_filename[sizeof (gui_state_filename) - 1] = '\0';
    pending_quick_save = 1;
}

/* Called from ami-gui.c to safely schedule a state restore. */
void gui_request_load (const char *path)
{
    if (!path || !path[0])
        return;
    strncpy (gui_state_filename, path, sizeof (gui_state_filename) - 1);
    gui_state_filename[sizeof (gui_state_filename) - 1] = '\0';
    pending_quick_load = 1;
}

/* For ami-gui.c: the emulator's window, and whether it sits on a public
 * screen (Workbench) rather than a fullscreen of its own. */
static struct Window *W;
static int usepub;

struct Window *ami_main_window (void)
{
    return W;
}

int ami_main_on_pubscreen (void)
{
    return W && usepub;
}
/* === FRF: 3-char quickstate code (0-9 / A-Z) ===================== */

/* Mode: 0 = idle, 1 = SAVE, 2 = LOAD */
static int  quickcode_mode  = 0;    /* current mode */
static int  quickcode_len   = 0;    /* how many chars we have (0..3) */
static char quickcode_chars[3];     /* the actual characters user typed */

/* Optional: call this when you want to completely clear the code state */
static void quickcode_reset(void)
{
    quickcode_mode = 0;
    quickcode_len  = 0;
    quickcode_chars[0] = quickcode_chars[1] = quickcode_chars[2] = '\0';
}

/* Start a new 3-char code entry.
 * mode: 1 = save, 2 = load
 */
static void quickcode_start(int mode)
{
    quickcode_reset();
    quickcode_mode = mode;

    write_log("QUICKSTATE: %s code – waiting for 3 chars (0-9/A-Z)\n",
              (mode == 1) ? "SAVE" : "LOAD");
}

/* Map Amiga rawkey -> '0'-'9' or 'A'-'Z'. Return 0 if not usable. */
static char quickcode_char_from_raw(int keycode)
{
    switch (keycode) {
        /* main number row 1–9, then 0 */
        case 0x01: return '1';
        case 0x02: return '2';
        case 0x03: return '3';
        case 0x04: return '4';
        case 0x05: return '5';
        case 0x06: return '6';
        case 0x07: return '7';
        case 0x08: return '8';
        case 0x09: return '9';
        case 0x0A: return '0';

        /* Q W E R T Y U I O P */
        case 0x10: return 'Q';
        case 0x11: return 'W';
        case 0x12: return 'E';
        case 0x13: return 'R';
        case 0x14: return 'T';
        case 0x15: return 'Y';
        case 0x16: return 'U';
        case 0x17: return 'I';
        case 0x18: return 'O';
        case 0x19: return 'P';

        /* A S D F G H J K L */
        case 0x20: return 'A';
        case 0x21: return 'S';
        case 0x22: return 'D';
        case 0x23: return 'F';
        case 0x24: return 'G';
        case 0x25: return 'H';
        case 0x26: return 'J';
        case 0x27: return 'K';
        case 0x28: return 'L';

        /* Z X C V B N M */
        case 0x31: return 'Z';
        case 0x32: return 'X';
        case 0x33: return 'C';
        case 0x34: return 'V';
        case 0x35: return 'B';
        case 0x36: return 'N';
        case 0x37: return 'M';
    }
    return 0;   /* not a code char */
}

/* Commit a full 3-char code.
 * Mode: 1 = SAVE, 2 = LOAD
 */
static void quickcode_commit(void)
{
    char fname[64];

    if (quickcode_len != 3 || quickcode_mode == 0) {
        write_log("QUICKSTATE: bad/incomplete code, abort\n");
        quickcode_reset();
        return;
    }

    /* Build filename EXACTLY from the 3 chars the user typed.
     *
     * Result: 68kEUAE_ABC.ss
     * (no dropping of leading zeros or anything; it's literal chars)
     */
    snprintf(fname, sizeof(fname),
             "%c%c%c.uss",
             quickcode_chars[0],
             quickcode_chars[1],
             quickcode_chars[2]);

    write_log("QUICKSTATE: %s '%s'\n",
              (quickcode_mode == 1) ? "SAVE" : "LOAD",
              fname);

    /* Initialise the savestate engine to use our filename.
     * We keep the 2-arg form here because this tree's savestate_initsave()
     * takes 2 params in ami-win.c.
     *
     * If your savestate.c later turns out to have a 3-arg form with
     * a "no dialogs" flag, we can flip to that and hard-wire nodialogs=1.
     */
    savestate_initsave(fname, 0);

    /* Use the existing quick-state helper.
     * Slot is still "1", but the underlying filename is now our 68kEUAE_***.ss.
     */
    savestate_quick(1, (quickcode_mode == 1) ? 1 : 0);

    quickcode_reset();
}

/* Feed one rawkey into the 3-char quick code. ESC cancels.
 * NOTE: handle_events() already only calls this on key-DOWN,
 * so we don't need the 'state' here.
 */
static void quickcode_feed_key(int rawkey)
{
    char c;

    if (!quickcode_mode)
        return;

    /* ESC (0x45) cancels the current code */
    if (rawkey == 0x45) {
        write_log("QUICKSTATE: code cancelled by ESC\n");
        quickcode_reset();
        return;
    }

    if (quickcode_len >= 3) {
        /* already full, wait for commit */
        return;
    }

    c = quickcode_char_from_raw(rawkey);
    if (!c) {
        /* Not a valid code char – ignore while in code mode */
        write_log("QUICKSTATE: ignoring non-code key 0x%02X\n", rawkey);
        return;
    }

    quickcode_chars[quickcode_len++] = c;
    write_log("QUICKSTATE: got '%c' (%d/3)\n", c, quickcode_len);

    if (quickcode_len == 3)
        quickcode_commit();
}



void gui_frameskip_set (int value)
{
    gui_data.frameskip = value;
}

void gui_frameskip_flash (int polarity)
{
    /* +1 = plus (flash RED), -1 = minus (flash GREEN), 0 = clear */
    gui_data.frameskip_flash_polarity = (polarity > 0) ? 2 : (polarity < 0 ? 1 : 0);
    gui_data.frameskip_flash_ticks = gui_data.frameskip_flash_polarity ? 6 : 0;
}


static int need_dither;        /* well.. guess :-) */
static int use_delta_buffer;   /* this will redraw only needed places */
static int use_cyb;            /* this is for cybergfx truecolor mode */
static int use_approx_color;

/* Scale factors: 1 = no scale, 2 = double, etc.
 * Set when window/screen is larger than the native Amiga render size. */
static int scale_x = 1;
static int scale_y = 1;

/* Requested display (window) size — may be larger than the native render
 * size when stretching is active. gfxvidinfo stays at native resolution. */
static int display_w = 0;
static int display_h = 0;

/* Persists across open_display() (which zeroes display_w/h) so
 * do_toggle_lores() can track which step of the 320->640->800->320
 * cycle it is at. 0 = not yet toggled (initial state).             */
static int last_display_w = 0;
static int last_display_h = 0;
static int pending_resize    = 0;  /* set in NEWSIZE, acted on next handle_events entry */
static int pending_toggle_fs  = 0;  /* deferred toggle_fullscreen */
static int pending_toggle_res = 0;  /* deferred toggle_lores */
static int pending_screenmode = 0;  /* deferred screenmode requester */
static int force_screenmode_req = 0; /* set to bypass get_displayid in setup_userscreen */

#ifdef PICASSO96
/* The emulated graphics card (uaegfx, picasso96.c): its picture goes into
 * p96buf, 32-bit ARGB, and from there to the window each frame. */
static int pending_p96 = 0;         /* deferred: the window for RTG on or off */
static int screen_is_picasso = 0;   /* the window shows the card, not the chipset */
static void p96_flush (void);
static void p96_invalidate_all (void);
static void do_p96_display (void);
static int p96buf_w, p96buf_h;          /* the card picture's size (defined below) */
static int p96_saved_dw;                /* the window was resized for the card */
#endif

/* Pause-on-focus-loss feature.
 * Set ami_pause_focus_loss=1 before entering ami_pause_loop() via
 * IDCMP_INACTIVEWINDOW so the loop auto-resumes on IDCMP_ACTIVEWINDOW.
 * Disabled at runtime by setting env var UAE_NO_PAUSE_ON_FOCUS_LOSS=1
 * in the shell before launching UAE (e.g.  SET UAE_NO_PAUSE_ON_FOCUS_LOSS 1). */
#define UAE_NO_PAUSE_ENV "UAE_NO_PAUSE_ON_FOCUS_LOSS"
static int ami_pause_focus_loss    = 0; /* set when pausing due to focus loss */
static int ami_no_pause_on_focus_loss = -1; /* -1 = uninitialised */


extern xcolnr xcolors[4096];
extern void notice_new_xcolors (void);

static uae_u8 *oldpixbuf;

/* Values for amiga_screen_type */
enum {
    UAESCREENTYPE_CUSTOM,
    UAESCREENTYPE_PUBLIC,
    UAESCREENTYPE_ASK,
    UAESCREENTYPE_LAST
};

/****************************************************************************/
/*
 * prototypes & global vars
 */

struct IntuitionBase    *IntuitionBase = NULL;
struct GfxBase          *GfxBase = NULL;

/* Input device for pointer warping (mouse grab in windowed mode) */
static struct MsgPort    *input_mp   = NULL;
static struct IOStdReq   *input_req  = NULL;
static int                input_open = 0;
struct Library          *LayersBase = NULL;
struct Library          *AslBase = NULL;
struct Library          *CyberGfxBase = NULL;

struct AslIFace *IAsl;
struct GraphicsIFace *IGraphics;
struct LayersIFace *ILayers;
struct IntuitionIFace *IIntuition;
struct CyberGfxIFace *ICyberGfx;

unsigned long            frame_num; /* for arexx */

static UBYTE            *Line;
static struct RastPort  *RP;
static struct Screen    *S;
static struct Window    *W;
static struct RastPort  *TempRPort;
static struct BitMap    *BitMap;
#ifdef USE_CYBERGFX
static uae_u8        *CybBuffer  = NULL;   /* v41+ chunky render buffer  */
static struct BitMap *CybBitMap  = NULL;   /* pre-v41 CGX render bitmap  */
static struct BitMap *ScaleBitMap = NULL;  /* pre-v41 scaled output bitmap */
static uae_u8        *CpuBuffer  = NULL;   /* pre-v41 CPU-RAM copy for safe reads */
static int            CpuBufRowbytes = 0;
#endif
static struct ColorMap  *CM;
static int              XOffset,YOffset;

static int os39;        /* kick 39 present */
static int usepub;      /* use public screen */
static int is_halfbrite;
static int is_ham;

static int   get_color_failed;
static int   maxpen;
static UBYTE pen[256];

static int mouseGrabbed  = 0;
static int grabTicks     = 0;
static int warpSkip      = 0;   /* deltas to discard after a pointer warp */
#define GRAB_TIMEOUT 50

static struct BitMap *myAllocBitMap(ULONG,ULONG,ULONG,ULONG,struct BitMap *);
static void set_title(void);
static void myFreeBitMap(struct BitMap *);
static LONG ObtainColor(ULONG, ULONG, ULONG);
static void ReleaseColors(void);
static int  DoSizeWindow(struct Window *,int,int);
static int  init_ham(void);
static void ham_conv(UWORD *src, UBYTE *buf, UWORD len);
static void close_display (void);
static void do_toggle_fullscreen (void);
static void do_toggle_lores (void);
static int  open_display (void);
static int  RPDepth(struct RastPort *RP);
static int get_nearest_color(int r, int g, int b);

/****************************************************************************/

void main_window_led(int led, int on);
int do_inhibit_frame(int onoff);

extern void initpseudodevices(void);
extern void closepseudodevices(void);
extern void appw_init(struct Window *W);
extern void appw_exit(void);
extern void appw_events(void);
extern void hostdisk_tick(void);    /* od-amiga/hostdisk.c */

extern void gui_toggle(void);        /* ami-gui.c - Ctrl+LAlt+B opens/closes GUI */
extern void gui_handle_events(void); /* ami-gui.c - pump GUI IDCMP while paused */
extern int  gui_screen_open;         /* ami-gui.c - 1 while GUI window is open  */

extern int ievent_alive;

/***************************************************************************
 *
 * Default hotkeys
 *
 * We need a better way of doing this. ;-)
 */
static struct uae_hotkeyseq ami_hotkeys[] =
{
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_Q, -1,      INPUTEVENT_SPC_QUIT) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_R, -1,      INPUTEVENT_SPC_SOFTRESET) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_LSH, AK_R,  INPUTEVENT_SPC_HARDRESET) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_D, -1,      INPUTEVENT_SPC_ENTERDEBUGGER) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_U, -1,      INPUTEVENT_SPC_TOGGLEFULLSCREEN) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_L, -1,      INPUTEVENT_SPC_TOGGLELORES) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_G, -1,      INPUTEVENT_SPC_TOGGLEMOUSEGRAB) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_I, -1,      INPUTEVENT_SPC_INHIBITSCREEN) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_P, -1,      INPUTEVENT_SPC_FREEZEBUTTON) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_A, -1,      INPUTEVENT_SPC_SWITCHINTERPOL) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_NPADD, -1,  INPUTEVENT_SPC_INCRFRAMERATE) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_NPSUB, -1,  INPUTEVENT_SPC_DECRFRAMERATE) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F1, -1,     INPUTEVENT_SPC_FLOPPY0) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F2, -1,     INPUTEVENT_SPC_FLOPPY1) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F3, -1,     INPUTEVENT_SPC_FLOPPY2) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F4, -1,     INPUTEVENT_SPC_FLOPPY3) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F5, -1,     INPUTEVENT_SPC_STATESAVEQUICK) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_F6, -1,     INPUTEVENT_SPC_STATERESTOREQUICK) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_LSH, AK_F1, INPUTEVENT_SPC_EFLOPPY0) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_LSH, AK_F2, INPUTEVENT_SPC_EFLOPPY1) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_LSH, AK_F3, INPUTEVENT_SPC_EFLOPPY2) },
    { MAKE_HOTKEYSEQ (AK_CTRL, AK_LALT, AK_LSH, AK_F4, INPUTEVENT_SPC_EFLOPPY3) },
    { HOTKEYS_END }
};

/****************************************************************************/

extern UBYTE cidx[4][8*4096];


/*
 * Dummy buffer locking methods
 */
static int dummy_lock (struct vidbuf_description *gfxinfo)
{
    return 1;
}

static void dummy_unlock (struct vidbuf_description *gfxinfo)
{
}

static void dummy_flush_screen (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
}


/*
 * Buffer methods for planar screens with no dithering.
 *
 * This uses a delta buffer to reduce the overhead of doing the chunky-to-planar
 * conversion required.
 */
STATIC_INLINE void flush_line_planar_nodither (struct vidbuf_description *gfxinfo, int line_no)
{
    int     xs      = 0;
    int     len     = gfxinfo->width;
    int     yoffset = line_no * gfxinfo->rowbytes;
    uae_u8 *src;
    uae_u8 *dst;
    uae_u8 *newp = gfxinfo->bufmem + yoffset;
    uae_u8 *oldp = oldpixbuf + yoffset;

    /* Find first pixel changed on this line */
    while (*newp++ == *oldp++) {
	if (!--len)
	    return; /* line not changed - so don't draw it */
    }
    src   = --newp;
    dst   = --oldp;
    newp += len;
    oldp += len;

    /* Find last pixel changed on this line */
    while (*--newp == *--oldp)
	;

    len = 1 + (oldp - dst);
    xs  = src - (uae_u8 *)(gfxinfo->bufmem + yoffset);

    /* Copy changed pixels to delta buffer */
    CopyMem (src, dst, len);

    /* Blit changed pixels to the display */
    WritePixelLine8 (RP, xs + XOffset, line_no + YOffset, len, dst, TempRPort);
}

static void flush_block_planar_nodither (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    int line_no;

    for (line_no = first_line; line_no <= last_line; line_no++)
	flush_line_planar_nodither (gfxinfo, line_no);
}

/*
 * Buffer methods for planar screens with dithering.
 *
 * This uses a delta buffer to reduce the overhead of doing the chunky-to-planar
 * conversion required.
 */
STATIC_INLINE void flush_line_planar_dither (struct vidbuf_description *gfxinfo, int line_no)
{
    int      xs      = 0;
    int      len     = gfxinfo->width;
    int      yoffset = line_no * gfxinfo->rowbytes;
    uae_u16 *src;
    uae_u16 *dst;
    uae_u16 *newp = (uae_u16 *)(gfxinfo->bufmem + yoffset);
    uae_u16 *oldp = (uae_u16 *)(oldpixbuf + yoffset);

    /* Find first pixel changed on this line */
    while (*newp++ == *oldp++) {
	if (!--len)
	    return; /* line not changed - so don't draw it */
    }
    src   = --newp;
    dst   = --oldp;
    newp += len;
    oldp += len;

    /* Find last pixel changed on this line */
    while (*--newp == *--oldp)
	;

    len = (1 + (oldp - dst));
    xs  = src - (uae_u16 *)(gfxinfo->bufmem + yoffset);

    /* Copy changed pixels to delta buffer */
    CopyMem (src, dst, len * 2);

    /* Dither changed pixels to Line buffer */
    DitherLine (Line, src, xs, line_no, (len + 3) & ~3, 8);

    /* Blit dithered pixels from Line buffer to the display */
    WritePixelLine8 (RP, xs + XOffset, line_no + YOffset, len, Line, TempRPort);
}

static void flush_block_planar_dither (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    int line_no;

    for (line_no = first_line; line_no <= last_line; line_no++)
	flush_line_planar_dither (gfxinfo, line_no);
}

/*
 * Buffer methods for HAM screens.
 */
STATIC_INLINE void flush_line_ham (struct vidbuf_description *gfxinfo, int line_no)
{
    int     len = gfxinfo->width;
    uae_u8 *src = gfxinfo->bufmem + (line_no * gfxinfo->rowbytes);

    ham_conv ((void*) src, Line, len);
    WritePixelLine8 (RP, 0, line_no, len, Line, TempRPort);

    return;
}

static void flush_block_ham (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    int line_no;

    for (line_no = first_line; line_no <= last_line; line_no++)
	flush_line_ham (gfxinfo, line_no);
}

#ifdef USE_CYBERGFX
# ifndef USE_CYBERGFX_V41
static void flush_line_cgx (struct vidbuf_description *gfxinfo, int line_no)
{
    BltBitMapRastPort (CybBitMap,
		       0, line_no,
		       RP,
		       XOffset,
		       YOffset + line_no,
		       gfxinfo->width,
		       1,
		       0xc0);
}

static void flush_block_cgx (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    BltBitMapRastPort (CybBitMap,
		       0, first_line,
		       RP,
		       XOffset,
		       YOffset + first_line,
		       gfxinfo->width,
		       last_line - first_line + 1,
		       0xc0);
}
# else
static void flush_line_cgx_v41 (struct vidbuf_description *gfxinfo, int line_no)
{
    WritePixelArray (CybBuffer,
		     0 , line_no,
		     gfxinfo->rowbytes,
		     RP,
		     XOffset,
		     YOffset + line_no,
		     gfxinfo->width,
		     1,
		     RECTFMT_RAW);
}

static void flush_block_cgx_v41 (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    WritePixelArray (CybBuffer,
		     0 , first_line,
		     gfxinfo->rowbytes,
		     RP,
		     XOffset,
		     YOffset + first_line,
		     gfxinfo->width,
		     last_line - first_line + 1,
		     RECTFMT_RAW);
}
# endif
#endif

#ifdef USE_CYBERGFX
/* Software nearest-neighbour scale buffer and its dimensions/stride.
 * Allocated when scale_x != gfxvidinfo.width || scale_y != gfxvidinfo.height. */
static uae_u8 *ScaleBuffer     = NULL;
static int     ScaleBufRowbytes = 0;

/* Scaled flush — software nearest-neighbour stretch from native render
 * buffer (CybBuffer, gfxinfo->width x gfxinfo->height) into ScaleBuffer
 * (scale_x x scale_y), then WritePixelArray/BltBitMapRastPort to screen.
 *
 * Works on any CGX version — no ScalePixelArray required.
 */
static void flush_block_cgx_scaled (struct vidbuf_description *gfxinfo, int first_line, int last_line)
{
    /* All declarations at top — C89 requirement */
    int src_w, src_h, dst_w, dst_h, pixbytes, dst_rb, src_rb;
    int dy, dx, row, dst_first, dst_last;
    unsigned long x_step, y_step, y_accum, x_accum;
    uae_u8 *src_base;
    uae_u8 *src_row;
    uae_u8 *dst_row;

    src_w    = gfxinfo->width;
    src_h    = gfxinfo->height;
    dst_w    = scale_x;
    dst_h    = scale_y;
    pixbytes = gfxinfo->pixbytes;
    dst_rb   = ScaleBufRowbytes;
    src_rb   = gfxinfo->rowbytes;
    src_base = (uae_u8 *)gfxinfo->bufmem;

    if (!ScaleBuffer || !src_base || !RP) return;

    /* Fixed-point 16.16 step — eliminates per-pixel integer division.
     * 68881 FPU used to compute steps when available (-m68881 build). */
#ifdef __mc68881__
    {
        double fx = (double)src_w / (double)dst_w;
        double fy = (double)src_h / (double)dst_h;
        x_step = (unsigned long)(fx * 65536.0 + 0.5);
        y_step = (unsigned long)(fy * 65536.0 + 0.5);
    }
#else
    x_step = ((unsigned long)src_w << 16) / (unsigned long)dst_w;
    y_step = ((unsigned long)src_h << 16) / (unsigned long)dst_h;
#endif

    /* Map dirty source line range to destination line range.
     * UAE passes only the changed band — no need to scale/blit the
     * entire frame every flush.  On PiStorm/Emu68 this is the key
     * optimisation: fewer bytes crossing the Zorro bus per frame. */
    {
    int dst_first, dst_last;
    dst_first = (first_line * dst_h) / src_h;
    dst_last  = (last_line  * dst_h) / src_h;
    if (dst_first < 0)       dst_first = 0;
    if (dst_last  >= dst_h)  dst_last  = dst_h - 1;

    for (dy = dst_first, y_accum = (unsigned long)dst_first * y_step;
         dy <= dst_last;
         dy++, y_accum += y_step) {
        src_row = src_base    + (y_accum >> 16) * src_rb;
        dst_row = ScaleBuffer + dy * dst_rb;

        if (pixbytes == 4) {
            uae_u32 *src32 = (uae_u32 *)src_row;
            uae_u32 *dst32 = (uae_u32 *)dst_row;
            for (dx = 0, x_accum = 0; dx < dst_w; dx++, x_accum += x_step)
                dst32[dx] = src32[x_accum >> 16];
        } else if (pixbytes == 2) {
            uae_u16 *src16 = (uae_u16 *)src_row;
            uae_u16 *dst16 = (uae_u16 *)dst_row;
            for (dx = 0, x_accum = 0; dx < dst_w; dx++, x_accum += x_step)
                dst16[dx] = src16[x_accum >> 16];
        } else {
            for (dx = 0, x_accum = 0; dx < dst_w; dx++, x_accum += x_step)
                dst_row[dx] = src_row[x_accum >> 16];
        }
    }

# ifdef USE_CYBERGFX_V41
    WritePixelArray (ScaleBuffer, 0, dst_first, dst_rb, RP,
                     XOffset, YOffset + dst_first,
                     dst_w, dst_last - dst_first + 1, RECTFMT_RAW);
# else
    if (ScaleBitMap) {
        uae_u8 *bm_dst = (uae_u8 *) GetCyberMapAttr (ScaleBitMap, CYBRMATTR_DISPADR);
        for (row = dst_first; row <= dst_last; row++)
            CopyMem (ScaleBuffer + row * dst_rb, bm_dst + row * dst_rb, dst_rb);
        BltBitMapRastPort (ScaleBitMap, 0, dst_first, RP,
                           XOffset, YOffset + dst_first,
                           dst_w, dst_last - dst_first + 1, 0xc0);
    }
# endif
    } /* end dirty band */
}

static void flush_line_cgx_scaled (struct vidbuf_description *gfxinfo, int line_no)
{
    (void)line_no;
    flush_block_cgx_scaled (gfxinfo, 0, gfxinfo->height - 1);
}
#endif

/****************************************************************************/

static void flush_clear_screen_gfxlib (struct vidbuf_description *gfxinfo)
{
    if (RP) {
#ifdef USE_CYBERGFX
	if (use_cyb)
	     FillPixelArray (RP, W->BorderLeft, W->BorderTop,
			     W->Width - W->BorderLeft - W->BorderRight,
			     W->Height - W->BorderTop - W->BorderBottom,
			     0);
        else
#endif
	{
	    SetAPen  (RP, get_nearest_color (0,0,0));
	    RectFill (RP, W->BorderLeft, W->BorderTop, W->Width - W->BorderRight,
		      W->Height - W->BorderBottom);
	}
    }
    if (use_delta_buffer)
        memset (oldpixbuf, 0, gfxinfo->rowbytes * gfxinfo->height);
}

/****************************************************************************/


static int RPDepth (struct RastPort *RP)
{
#ifdef USE_CYBERGFX
    if (use_cyb)
	return GetCyberMapAttr (RP->BitMap, (LONG)CYBRMATTR_DEPTH);
#endif
    return RP->BitMap->Depth;
}

/****************************************************************************/

static int get_color (int r, int g, int b, xcolnr *cnp)
{
    int col;

    if (currprefs.amiga_use_grey)
	r = g = b = (77 * r + 151 * g + 29 * b) / 16;
    else {
	r *= 0x11;
	g *= 0x11;
	b *= 0x11;
    }

    r *= 0x01010101;
    g *= 0x01010101;
    b *= 0x01010101;
    col = ObtainColor (r, g, b);

    if (col == -1) {
	get_color_failed = 1;
	return 0;
    }

    *cnp = col;
    return 1;
}

/****************************************************************************/
/*
 * FIXME: find a better way to determine closeness of colors (closer to
 * human perception).
 */
static __inline__ void rgb2xyz (int r, int g, int b,
				int *x, int *y, int *z)
{
    *x = r * 1024 - (g + b) * 512;
    *y = 886 * (g - b);
    *z = (r + g + b) * 341;
}

static __inline__ int calc_err (int r1, int g1, int b1,
				int r2, int g2, int b2)
{
    int x1, y1, z1, x2, y2, z2;

    rgb2xyz (r1, g1, b1, &x1, &y1, &z1);
    rgb2xyz (r2, g2, b2, &x2, &y2, &z2);
    x1 -= x2; y1 -= y2; z1 -= z2;
    return x1 * x1 + y1 * y1 + z1 * z1;
}

/****************************************************************************/

static int get_nearest_color (int r, int g, int b)
{
    int i, best, err, besterr;
    int colors;
    int br=0,bg=0,bb=0;

   if (currprefs.amiga_use_grey)
	r = g = b = (77 * r + 151 * g + 29 * b) / 256;

    best    = 0;
    besterr = calc_err (0, 0, 0, 15, 15, 15);
    colors  = is_halfbrite ? 32 :(1 << RPDepth (RP));

    for (i = 0; i < colors; i++) {
	long rgb;
	int cr, cg, cb;

	rgb = GetRGB4 (CM, i);

	cr = (rgb >> 8) & 15;
	cg = (rgb >> 4) & 15;
	cb = (rgb >> 0) & 15;

	err = calc_err (r, g, b, cr, cg, cb);

	if(err < besterr) {
	    best = i;
	    besterr = err;
	    br = cr; bg = cg; bb = cb;
	}

	if (is_halfbrite) {
	    cr /= 2; cg /= 2; cb /= 2;
	    err = calc_err (r, g, b, cr, cg, cb);
	    if (err < besterr) {
		best = i + 32;
		besterr = err;
		br = cr; bg = cg; bb = cb;
	    }
	}
    }
    return best;
}

/****************************************************************************/

#ifdef USE_CYBERGFX
static int init_colors_cgx (const struct RastPort *rp)
{
    int redbits,  greenbits,  bluebits;
    int redshift, greenshift, blueshift;
    int byte_swap = FALSE;
    int pixfmt;
    int found = TRUE;

    pixfmt = GetCyberMapAttr (rp->BitMap, (LONG)CYBRMATTR_PIXFMT);

    switch (pixfmt) {
#ifdef WORDS_BIGENDIAN
	case PIXFMT_RGB15PC:
	    byte_swap = TRUE;
	case PIXFMT_RGB15:
	    redbits  = 5;  greenbits  = 5; bluebits  = 5;
	    redshift = 10; greenshift = 5; blueshift = 0;
	    break;
	case PIXFMT_RGB16PC:
	    byte_swap = TRUE;
	case PIXFMT_RGB16:
	    redbits  = 5;  greenbits  = 6;  bluebits  = 5;
	    redshift = 11; greenshift = 5;  blueshift = 0;
	    break;
	case PIXFMT_RGBA32:
	    redbits  = 8;  greenbits  = 8;  bluebits  = 8;
	    redshift = 24; greenshift = 16; blueshift = 8;
	    break;
	case PIXFMT_BGRA32:
	    redbits  = 8;  greenbits  = 8;  bluebits  = 8;
	    redshift = 8;  greenshift = 16; blueshift = 24;
	    break;
	case PIXFMT_ARGB32:
	    redbits  = 8;  greenbits  = 8;  bluebits  = 8;
	    redshift = 16; greenshift = 8;  blueshift = 0;
	    break;
#else
	case PIXFMT_RGB15:
	    byte_swap = TRUE;
	case PIXFMT_RGB15PC:
	    redbits  = 5;  greenbits  = 5;  bluebits  = 5;
	    redshift = 10; greenshift = 0;  blueshift = 0;
	    break;
	case PIXFMT_RGB16:
	    byte_swap = TRUE;
	case PIXFMT_RGB16PC:
	    redbits  = 5;  greenbits  = 6;  bluebits  = 5;
	    redshift = 11; greenshift = 5;  blueshift = 0;
	    break;
	case PIXFMT_BGRA32:
	    redbits  = 8;  greenbits  = 8;  bluebits  = 8;
	    redshift = 16; greenshift = 8;  blueshift = 0;
	    break;
	case PIXFMT_ARGB32:
	    redbits  = 8;  greenbits  = 8;  bluebits  = 8;
	    redshift = 8;  greenshift = 16; blueshift = 24;
	    break;
#endif
	default:
	    redbits  = 0;  greenbits  = 0;  bluebits  = 0;
	    redshift = 0;  greenshift = 0;  blueshift = 0;
	    found = FALSE;
	    break;
    }

    if (found) {
	alloc_colors64k (redbits,  greenbits,  bluebits,
			 redshift, greenshift, blueshift,
			 0, 0, 0, byte_swap);

	write_log ("AMIGFX: Using a %d-bit true-colour display.\n",
		   redbits + greenbits + bluebits);
    } else
	write_log ("AMIGFX: Unsupported pixel format.\n");

    return found;
}
#endif

static int init_colors (void)
{
    int success = TRUE;

    if (need_dither) {
	/* first try color allocation */
	int bitdepth = usepub ? 8 : RPDepth (RP);
	int maxcol;

	if (!currprefs.amiga_use_grey && bitdepth >= 3)
	    do {
		get_color_failed = 0;
		setup_dither (bitdepth, get_color);
		if (get_color_failed)
		    ReleaseColors ();
	    } while (get_color_failed && --bitdepth >= 3);

	if( !currprefs.amiga_use_grey && bitdepth >= 3) {
	    write_log ("AMIGFX: Color dithering with %d bits\n", bitdepth);
	    return 1;
	}

	/* if that fail then try grey allocation */
	maxcol = 1 << (usepub ? 8 : RPDepth (RP));

	do {
	    get_color_failed = 0;
	    setup_greydither_maxcol (maxcol, get_color);
	    if (get_color_failed)
		ReleaseColors ();
	} while (get_color_failed && --maxcol >= 2);

	/* extra pass with approximated colors */
	if (get_color_failed)
	    do {
		maxcol=2;
		use_approx_color = 1;
		get_color_failed = 0;
		setup_greydither_maxcol (maxcol, get_color);
		if (get_color_failed)
		    ReleaseColors ();
	    } while (get_color_failed && --maxcol >= 2);

	if (maxcol >= 2) {
	    write_log ("AMIGFX: Grey dithering with %d shades.\n", maxcol);
	    return 1;
	}

	return 0; /* everything failed :-( */
    }

    /* No dither */
    switch (RPDepth (RP)) {
	case 6:
	    if (is_halfbrite) {
		static int tab[]= {
		    0x000, 0x00f, 0x0f0, 0x0ff, 0x08f, 0x0f8, 0xf00, 0xf0f,
		    0x80f, 0xff0, 0xfff, 0x88f, 0x8f0, 0x8f8, 0x8ff, 0xf08,
		    0xf80, 0xf88, 0xf8f, 0xff8, /* end of regular pattern */
		    0xa00, 0x0a0, 0xaa0, 0x00a, 0xa0a, 0x0aa, 0xaaa,
		    0xfaa, 0xf6a, 0xa80, 0x06a, 0x6af
		};
		int i;
		for (i = 0; i < 32; ++i)
		    get_color (tab[i] >> 8, (tab[i] >> 4) & 15, tab[i] & 15, xcolors);
		for (i = 0; i < 4096; ++i) {
		    uae_u32 val = get_nearest_color (i >> 8, (i >> 4) & 15, i & 15);
		    xcolors[i] = val * 0x01010101;
		}
		write_log ("AMIGFX: Using 32 colours and half-brite\n");
		break;
	    } else if (is_ham) {
		int i;
		for (i = 0; i < 16; ++i)
		    get_color (i, i, i, xcolors);
		write_log ("AMIGFX: Using 12 bits pseudo-truecolor (HAM).\n");
		alloc_colors64k (4, 4, 4, 10, 5, 0, 0, 0, 0, 0);
		return init_ham ();
	    }
	    /* Fall through if !is_halfbrite && !is_ham */
	case 1: case 2: case 3: case 4: case 5: case 7: case 8: {
	    int maxcol = 1 << RPDepth (RP);
	    if (maxcol >= 8 && !currprefs.amiga_use_grey)
		do {
		    get_color_failed = 0;
		    setup_maxcol (maxcol);
		    alloc_colors256 (get_color);
		    if (get_color_failed)
			ReleaseColors ();
		} while (get_color_failed && --maxcol >= 8);
	    else {
		int i;
		for (i = 0; i < maxcol; ++i) {
		    get_color ((i * 15)/(maxcol - 1), (i * 15)/(maxcol - 1),
			       (i * 15)/(maxcol - 1), xcolors);
		}
	    }
	    write_log ("AMIGFX: Using %d colours.\n", maxcol);
	    for (maxcol = 0; maxcol < 4096; ++maxcol) {
		int val = get_nearest_color (maxcol >> 8, (maxcol >> 4) & 15, maxcol & 15);
		xcolors[maxcol] = val * 0x01010101;
	    }
	    break;
	}
#ifdef USE_CYBERGFX
	case 15:
	case 16:
	case 24:
	case 32:
	    success = init_colors_cgx (RP);
	    break;
#endif
    }
    return success;
}

/****************************************************************************/

static APTR blank_pointer;

/* Blank sprite data for SetPointer() — 1 word wide, 1 line tall, all zero.
 * Must be CHIP RAM, UWORD-aligned, and have a 2-word header + 2-word terminator. */
static UWORD blank_sprite_data[6];  /* header(2) + 1 line(2) + terminator(2) */

/*
 * Initializes a pointer object containing a blank pointer image.
 * Used for hiding the mouse pointer via SetWindowPointer (OS3.9+ / RTG).
 */
static void init_pointer (void)
{
    static struct BitMap bitmap;
    static UWORD	 row[2] = {0, 0};

    InitBitMap (&bitmap, 2, 16, 1);
    bitmap.Planes[0] = (PLANEPTR) &row[0];
    bitmap.Planes[1] = (PLANEPTR) &row[1];

    blank_pointer = NewObject (NULL, POINTERCLASS,
			       POINTERA_BitMap,	(ULONG)&bitmap,
			       POINTERA_WordWidth,	1,
			       TAG_DONE);

    if (!blank_pointer)
	write_log ("Warning: Unable to allocate blank mouse pointer.\n");

    /* Zero the sprite data — header, one blank line, terminator */
    blank_sprite_data[0] = 0;
    blank_sprite_data[1] = 0;
    blank_sprite_data[2] = 0;
    blank_sprite_data[3] = 0;
    blank_sprite_data[4] = 0;
    blank_sprite_data[5] = 0;
}

/*
 * Free up blank pointer object
 */
static void free_pointer (void)
{
    if (blank_pointer) {
	DisposeObject (blank_pointer);
	blank_pointer = NULL;
    }
}

/*
 * Hide mouse pointer for window
 */
static void hide_pointer (struct Window *w)
{
    if (!w) return;
    /* SetPointer() works on all OS3 screen types including RTG.
     * Use a 1x1 blank sprite — height=1, width=16, offsets=0. */
    SetPointer (w, blank_sprite_data, 1, 16, 0, 0);
}

/*
 * Restore default mouse pointer for window
 */
static void show_pointer (struct Window *w)
{
    if (!w) return;
    ClearPointer (w);
}

/*
 * Warp the mouse pointer to the centre of the window using IND_WRITEEVENT.
 * Used in windowed grab mode to keep the pointer inside the window.
 */
static void warp_pointer_to_center (void)
{
    struct InputEvent ie;
    if (!W || !input_open) return;
    ie.ie_NextEvent    = NULL;
    ie.ie_Class        = IECLASS_POINTERPOS;
    ie.ie_SubClass     = 0;
    ie.ie_Code         = IECODE_NOBUTTON;
    ie.ie_Qualifier    = 0;
    ie.ie_position.ie_xy.ie_x = (WORD)(W->LeftEdge + W->BorderLeft  + (W->Width  - W->BorderLeft - W->BorderRight)  / 2);
    ie.ie_position.ie_xy.ie_y = (WORD)(W->TopEdge  + W->BorderTop   + (W->Height - W->BorderTop  - W->BorderBottom) / 2);
    input_req->io_Command = IND_WRITEEVENT;
    input_req->io_Length  = sizeof (struct InputEvent);
    input_req->io_Data    = &ie;
    DoIO ((struct IORequest *)input_req);
    /* The warp generates a DELTAMOVE event — skip it so it doesn't
     * get fed to the emulated mouse as real movement */
    warpSkip = 2;
}

/*
 * Grab/release the mouse pointer.
 * On OS3: hide the pointer when grabbed (fullscreen backdrop window
 * already prevents the mouse from escaping — no extra confinement needed).
 * On OS4: additionally use WA_GrabFocus for a hard grab.
 * Call with mouseGrabbed already set to the desired state.
 */
static void grab_pointer (struct Window *w)
{
    if (!w) return;
    if (mouseGrabbed) {
        hide_pointer (w);
#ifdef __amigaos4__
        {
            struct IBox box;
            box.Left   = w->LeftEdge + w->BorderLeft;
            box.Top    = w->TopEdge  + w->BorderTop;
            box.Width  = w->Width  - w->BorderLeft - w->BorderRight;
            box.Height = w->Height - w->BorderTop  - w->BorderBottom;
            SetWindowAttrs (w, WA_MouseLimits, (ULONG)&box, sizeof box);
            SetWindowAttrs (w, WA_GrabFocus, (ULONG)GRAB_TIMEOUT, sizeof (ULONG));
        }
#endif
    } else {
        show_pointer (w);
#ifdef __amigaos4__
        {
            struct Screen *scr = w->WScreen;
            struct IBox box;
            box.Left   = 0;
            box.Top    = 0;
            box.Width  = scr ? scr->Width  : 16383;
            box.Height = scr ? scr->Height : 16383;
            SetWindowAttrs (w, WA_MouseLimits, (ULONG)&box, sizeof box);
            SetWindowAttrs (w, WA_GrabFocus, 0UL, sizeof (ULONG));
        }
#endif
    }
}

/****************************************************************************/

typedef enum {
    DONT_KNOW = -1,
    INSIDE_WINDOW,
    OUTSIDE_WINDOW
} POINTER_STATE;

static POINTER_STATE pointer_state;

static POINTER_STATE get_pointer_state (const struct Window *w, int mousex, int mousey)
{
    POINTER_STATE new_state = OUTSIDE_WINDOW;

    /*
     * Is pointer within the bounds of the inner window?
     */
    if ((mousex >= w->BorderLeft)
     && (mousey >= w->BorderTop)
     && (mousex < (w->Width - w->BorderRight))
     && (mousey < (w->Height - w->BorderBottom))) {
	/*
	 * Yes. Now check whetehr the window is obscured by
	 * another window at the pointer position
	 */
	struct Screen *scr = w->WScreen;
	struct Layer  *layer;

	/* Find which layer the pointer is in */
	LockLayerInfo (&scr->LayerInfo);
	layer = WhichLayer (&scr->LayerInfo, scr->MouseX, scr->MouseY);
	UnlockLayerInfo (&scr->LayerInfo);

	/* Is this layer our window's layer? */
	if (layer == w->WLayer) {
	    /*
	     * Yes. Therefore, pointer is inside the window.
	     */
	    new_state = INSIDE_WINDOW;
	}
    }
    return new_state;
}

/****************************************************************************/

#ifdef USE_CYBERGFX
/*
 * Try to find a CGX/P96 screen mode which suits the requested size and depth
 */
static ULONG find_rtg_mode (ULONG *width, ULONG *height, ULONG depth)
{
    ULONG mode           = INVALID_ID;

    ULONG best_mode      = INVALID_ID;
    ULONG best_width     = (ULONG) -1L;
    ULONG best_height    = (ULONG) -1L;

    ULONG largest_mode   = INVALID_ID;
    ULONG largest_width  = 0;
    ULONG largest_height = 0;

#ifdef DEBUG
    write_log ("Looking for RTG mode: w:%ld h:%ld d:%d\n", width, height, depth);
#endif

    if (CyberGfxBase) {
	while ((mode = NextDisplayInfo (mode)) != (ULONG)INVALID_ID) {
	    if (IsCyberModeID (mode)) {
		ULONG cwidth  = GetCyberIDAttr (CYBRIDATTR_WIDTH, mode);
		ULONG cheight = GetCyberIDAttr (CYBRIDATTR_HEIGHT, mode);
		ULONG cdepth  = GetCyberIDAttr (CYBRIDATTR_DEPTH, mode);
#ifdef DEBUG
		write_log ("Checking mode:%08x w:%d h:%d d:%d -> ", mode, cwidth, cheight, cdepth);
#endif
		if (cdepth == depth) {
		    /*
		     * If this mode has the largest screen size we've seen so far,
		     * remember it, just in case we don't find one big enough
		     */
		    if (cheight >= largest_height && cwidth >= largest_width) {
			largest_mode   = mode;
			largest_width  = cwidth;
			largest_height = cheight;
		    }

		    /*
		     * Is it large enough for our requirements?
		     */
		    if (cwidth >= *width && cheight >= *height) {
#ifdef DEBUG
			write_log ("large enough\n");
#endif
			/*
			 * Yes. Is it the best fit that we've seen so far?
			 */
			if (cwidth <= best_width && cheight <= best_height) {
			    best_width  = cwidth;
			    best_height = cheight;
			    best_mode   = mode;
			}
		    }
#ifdef DEBUG
		    else
			write_log ("too small\n");
#endif

		} /* if (cdepth == depth) */
#ifdef DEBUG
		else
		    write_log ("wrong depth\n");
#endif
	    } /* if (IsCyberModeID (mode)) */
	} /* while */

	if (best_mode != (ULONG)INVALID_ID) {
#ifdef DEBUG
	    write_log ("Found match!\n");
#endif
	    /* We found a match. Return it */
	    *height = best_height;
	    *width  = best_width;
	} else if (largest_mode != (ULONG)INVALID_ID) {
#ifdef DEBUG
	    write_log ("No match found!\n");
#endif
	    /* We didn't find a large enough mode. Return the largest
	     * mode found at the depth - if we found one */
	    best_mode = largest_mode;
	    *height   = largest_height;
	    *width    = largest_width;
	}
#ifdef DEBUG
	if (best_mode != (ULONG) INVALID_ID)
	    write_log ("Best mode: %08x w:%d h:%d d:%d\n", best_mode, *width, *height, depth);
#endif
    }
    return best_mode;
}
#endif

static int setup_customscreen (void)
{
    static struct NewWindow NewWindowStructure = {
	0, 0, 800, 600, 0, 1,
	IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_DISKINSERTED | IDCMP_DISKREMOVED
		| IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW | IDCMP_MOUSEMOVE
		| IDCMP_DELTAMOVE,
	WFLG_SMART_REFRESH | WFLG_BACKDROP | WFLG_RMBTRAP | WFLG_NOCAREREFRESH
	 | WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_REPORTMOUSE,
	NULL, NULL, NULL, NULL, NULL, 5, 5, 800, 600,
	CUSTOMSCREEN
    };

    ULONG width  = gfxvidinfo.width;
    ULONG height = gfxvidinfo.height;
    ULONG depth  = 0; // FIXME: Need to add some way of letting user specify preferred depth
    ULONG mode   = INVALID_ID;
    struct Screen *screen;
    ULONG error;

#ifdef USE_CYBERGFX
    /* First try to find an RTG screen that matches the requested size  */
    {
	unsigned int i;
	const UBYTE preferred_depth[] = {15, 16, 32, 8}; /* Try depths in this order of preference */

	for (i = 0; i < sizeof preferred_depth && mode == (ULONG) INVALID_ID; i++) {
	    depth = preferred_depth[i];
	    mode = find_rtg_mode (&width, &height, depth);
	}
    }

    if (mode != (ULONG) INVALID_ID) {
	if (depth > 8)
	    use_cyb = 1;
    } else {
#endif
	/* No (suitable) RTG screen available. Try a native mode */
	depth = os39 ? 8 : (currprefs.gfx_lores ? 5 : 4);
	mode = PAL_MONITOR_ID; // FIXME: should check whether to use PAL or NTSC.
	if (currprefs.gfx_lores)
	    mode |= (gfxvidinfo.height > 256) ? LORESLACE_KEY : LORES_KEY;
	else
	    mode |= (gfxvidinfo.height > 256) ? HIRESLACE_KEY : HIRES_KEY;
#ifdef USE_CYBERGFX
    }
#endif

    /* If the screen is larger than requested, centre UAE's display */
    if (width > (ULONG) gfxvidinfo.width)
	XOffset = (width - gfxvidinfo.width) / 2;
    if (height > (ULONG) gfxvidinfo.height)
	YOffset = (height - gfxvidinfo.height) / 2;

    do {
	screen = OpenScreenTags (NULL,
				 SA_Width,     width,
				 SA_Height,    height,
				 SA_Depth,     depth,
				 SA_DisplayID, mode,
				 SA_Behind,    TRUE,
				 SA_ShowTitle, FALSE,
				 SA_Quiet,     TRUE,
				 SA_ErrorCode, (ULONG)&error,
				 TAG_DONE);
    } while (!screen && error == OSERR_TOODEEP && --depth > 1); /* Keep trying until we find a supported depth */

    if (!screen) {
	/* TODO; Make this error report more useful based on the error code we got */
	write_log ("Error opening screen:%ld\n", error);
	gui_message ("Cannot open custom screen for UAE.\n");
	return 0;
    }

    S  = screen;
    CM = screen->ViewPort.ColorMap;
    RP = &screen->RastPort;

    NewWindowStructure.Width  = screen->Width;
    NewWindowStructure.Height = screen->Height;
    NewWindowStructure.Screen = screen;

    W = (void*)OpenWindow (&NewWindowStructure);
    if (!W) {
	write_log ("Cannot open UAE window on custom screen.\n");
	return 0;
    }

    hide_pointer (W);

    return 1;
}

/****************************************************************************/

static int setup_publicscreen(void)
{
    UWORD ZoomArray[4] = {0, 0, 0, 0};
    char *pubscreen = strlen (currprefs.amiga_publicscreen)
	? currprefs.amiga_publicscreen : NULL;
    /* Window size: display_w/h if scaling requested, else render size */
    int win_w = (display_w > 0) ? display_w : gfxvidinfo.width;
    int win_h = (display_h > 0) ? display_h : gfxvidinfo.height;
    int render_w = gfxvidinfo.width;
    int render_h = gfxvidinfo.height;

    S = LockPubScreen (pubscreen);
    if (!S) {
	gui_message ("Cannot open UAE window on public screen '%s'\n",
		pubscreen ? pubscreen : "default");
	return 0;
    }

    ZoomArray[2] = 128;
    ZoomArray[3] = S->BarHeight + 1;

    CM = S->ViewPort.ColorMap;

    if ((S->ViewPort.Modes & (HIRES | LACE)) == HIRES) {
	if (win_h + S->BarHeight + 1 >= S->Height) {
	    win_h >>= 1;
	    currprefs.gfx_correct_aspect = 1;
	}
    }

    W = OpenWindowTags (NULL,
			WA_Title,        (ULONG)PACKAGE_NAME,
			WA_AutoAdjust,   TRUE,
			WA_InnerWidth,   win_w,
			WA_InnerHeight,  win_h,
			WA_PubScreen,    (ULONG)S,
			WA_Zoom,         (ULONG)ZoomArray,
			WA_MinWidth,     160,
			WA_MinHeight,    100,
			WA_MaxWidth,     S->Width,
			WA_MaxHeight,    S->Height - S->BarHeight - 1,
			WA_IDCMP,        IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY
				       | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW
				       | IDCMP_MOUSEMOVE    | IDCMP_DELTAMOVE
				       | IDCMP_CLOSEWINDOW  | IDCMP_REFRESHWINDOW
				       | IDCMP_NEWSIZE      | IDCMP_INTUITICKS,
			WA_Flags,	 WFLG_DRAGBAR     | WFLG_DEPTHGADGET
				       | WFLG_REPORTMOUSE | WFLG_RMBTRAP
				       | WFLG_ACTIVATE    | WFLG_CLOSEGADGET
				       | WFLG_SIZEGADGET  | WFLG_SIZEBBOTTOM
				       | WFLG_SMART_REFRESH,
			TAG_DONE);

    UnlockPubScreen (NULL, S);

    if (!W) {
	write_log ("Can't open window on public screen!\n");
	CM = NULL;
	return 0;
    }

    /* Restore render resolution — scaling happens in flush, not here */
    gfxvidinfo.width  = render_w;
    gfxvidinfo.height = render_h;
    /* Scaled output fills the whole window inner area from top-left */
    XOffset = W->BorderLeft;
    YOffset = W->BorderTop;
    /* Consume display_w/h here so open_display scale computation uses
     * win_w/win_h directly rather than the original display_w/h values */
    display_w = win_w;
    display_h = win_h;

    RP = W->RPort;

    appw_init (W);

#ifdef USE_CYBERGFX
    if (CyberGfxBase && GetCyberMapAttr (RP->BitMap, (LONG)CYBRMATTR_ISCYBERGFX) &&
			(GetCyberMapAttr (RP->BitMap, (LONG)CYBRMATTR_DEPTH) > 8)) {
	use_cyb = 1;
    }

#endif

    return 1;
}

/****************************************************************************/

static char *get_num (char *s, int *n)
{
   int i=0;
   while(isspace(*s)) ++s;
   if(*s=='0') {
     ++s;
     if(*s=='x' || *s=='X') {
       do {char c=*++s;
           if(c>='0' && c<='9') {i*=16; i+= c-'0';}    else
           if(c>='a' && c<='f') {i*=16; i+= c-'a'+10;} else
           if(c>='A' && c<='F') {i*=16; i+= c-'A'+10;} else break;
       } while(1);
     } else while(*s>='0' && *s<='7') {i*=8; i+= *s++ - '0';}
   } else {
     while(*s>='0' && *s<='9') {i*=10; i+= *s++ - '0';}
   }
   *n=i;
   while(isspace(*s)) ++s;
   return s;
}

/****************************************************************************/

#include <stdlib.h>  /* strtoul */

static void get_displayid (ULONG *DI, LONG *DE)
{
    char *s, *end;
    unsigned long di, de;

    *DI = INVALID_ID;
    *DE = 0;

#ifdef PICASSO96
    /* The card's picture showing: a mode of its size, the deepest there is
     * (its colours are 32-bit). None: the chipset's choice below. */
    if (screen_is_picasso && p96buf_w > 0 && CyberGfxBase) {
        ULONG mode = INVALID_ID, best = INVALID_ID;
        int bestdepth = 0;
        while ((mode = NextDisplayInfo (mode)) != (ULONG)INVALID_ID) {
            struct DimensionInfo dim;
            int mw, mh, md;
            if (!IsCyberModeID (mode))
                continue;
            if (!GetDisplayInfoData (NULL, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, mode))
                continue;
            mw = dim.Nominal.MaxX - dim.Nominal.MinX + 1;
            mh = dim.Nominal.MaxY - dim.Nominal.MinY + 1;
            md = GetCyberIDAttr (CYBRIDATTR_DEPTH, mode);
            if (mw == p96buf_w && mh == p96buf_h && md >= 15 && md > bestdepth) {
                best = mode;
                bestdepth = md;
            }
        }
        if (best != (ULONG)INVALID_ID) {
            *DI = best;
            *DE = bestdepth;
            write_log ("DISPLAY: card picture %dx%d -> mode 0x%08lx depth %d\n",
                       p96buf_w, p96buf_h, (unsigned long)best, bestdepth);
            return;
        }
        write_log ("DISPLAY: no %dx%d RTG mode for the card's picture\n", p96buf_w, p96buf_h);
    }
#endif

    s = getenv(UAESM);

    /* FRF: hard default when UAESM isn't provided —
     * Find a CGX mode matching the required size (320x256 lores, 640x512 hires).
     * Walk NextDisplayInfo to find the first mode that fits rather than
     * hardcoding an ID that may not exist on this system. */
    if (!s || !*s) {
        int want_w = currprefs.gfx_lores ? 320 : 640;
        int want_h = currprefs.gfx_lores ? 256 : 512;
        ULONG mode = INVALID_ID;
        ULONG best = INVALID_ID;

        /* First try the hardcoded IDs — they work on most CGX setups */
        {
            ULONG try_id = currprefs.gfx_lores
                           ? (ULONG)0x50191101UL
                           : (ULONG)0x501e1101UL;
            struct DisplayInfo disp;
            if (GetDisplayInfoData(NULL, (UBYTE*)&disp, sizeof(disp), DTAG_DISP, try_id)) {
                best = try_id;
                write_log("DISPLAY: hardcoded mode 0x%08lx ok\n", (unsigned long)best);
            }
        }

        /* If hardcoded ID not found, scan for a CGX mode of the right size */
        if (best == (ULONG)INVALID_ID) {
            write_log("DISPLAY: scanning for %dx%d CGX mode...\n", want_w, want_h);
            mode = INVALID_ID;
            while ((mode = NextDisplayInfo(mode)) != (ULONG)INVALID_ID) {
                struct DimensionInfo dim;
                int mw, mh;
                if (!IsCyberModeID(mode)) continue;
                if (!GetDisplayInfoData(NULL,(UBYTE*)&dim,sizeof(dim),DTAG_DIMS,mode)) continue;
                mw = dim.Nominal.MaxX - dim.Nominal.MinX + 1;
                mh = dim.Nominal.MaxY - dim.Nominal.MinY + 1;
                if (mw == want_w && mh == want_h) {
                    best = mode;
                    write_log("DISPLAY: found matching mode 0x%08lx (%dx%d)\n",
                              (unsigned long)best, mw, mh);
                    break;
                }
            }
        }

        if (best == (ULONG)INVALID_ID) {
            write_log("DISPLAY: no %dx%d mode found, using INVALID_ID\n", want_w, want_h);
        }
        *DI = best;
        *DE = (LONG)15;
        write_log("DISPLAY: using screenmode 0x%08lx depth %ld for %dx%d\n",
                  (unsigned long)*DI, (long)*DE, want_w, want_h);
        return;
    }

    di = strtoul(s, &end, 0);          /* accepts 0x... or decimal */
    if (!end || *end != ':') {
        write_log("DISPLAY: UAESM malformed '%s' (expected <id>:<depth>)\n", s);
        return;
    }

    de = strtoul(end + 1, &end, 0);
    if (de == 0 || de > 32) {
        write_log("DISPLAY: UAESM depth invalid '%s'\n", s);
        return;
    }

    *DI = (ULONG)di;
    *DE = (LONG)de;

    write_log("DISPLAY: UAESM='%s' -> DisplayID=0x%08lx Depth=%ld\n",
              s, (unsigned long)*DI, (long)*DE);
}


/****************************************************************************/
static int get_mode_nominal_size(ULONG displayID, LONG *w, LONG *h)
{
    struct DimensionInfo dim;
    ULONG got = GetDisplayInfoData(NULL, (UBYTE*)&dim, sizeof(dim), DTAG_DIMS, displayID);
    if (got < sizeof(dim))
        return 0;

    *w = (LONG)(dim.Nominal.MaxX - dim.Nominal.MinX + 1);
    *h = (LONG)(dim.Nominal.MaxY - dim.Nominal.MinY + 1);
    return (*w > 0 && *h > 0);
}

static int setup_userscreen (void)
{
    struct ScreenModeRequester *ScreenRequest;
    ULONG DisplayID;
    LONG ScreenWidth = 0, ScreenHeight = 0, Depth = 0;
    UWORD OverscanType = OSCAN_STANDARD;
    BOOL AutoScroll = TRUE;
    int release_asl = 0;

    rexx_init();

    if (!AslBase) {
	AslBase = OpenLibrary ("asl.library", 36);
	if (!AslBase) {
	    write_log ("Can't open asl.library v36.\n");
	    return 0;
	} else {
#ifdef __amigaos4__
	    IAsl = (struct AslIFace *) GetInterface ((struct Library *)AslBase, "main", 1, NULL);
	    if (!IAsl) {
		CloseLibrary (AslBase);
		AslBase = 0;
		write_log ("Can't get asl.library interface\n");
	    }
#endif
	}
#ifdef __amigaos4__
    } else {
        IAsl->Obtain ();
        release_asl = 1;
#endif
    }

    ScreenRequest = AllocAslRequest (ASL_ScreenModeRequest, NULL);

    if (!ScreenRequest) {
	write_log ("Unable to allocate screen mode requester.\n");
	return 0;
    }

    get_displayid (&DisplayID, &Depth);

    /* When called from the GUI "Screen Mode..." button, always show the
     * requester regardless of whether get_displayid found a valid mode.  */
    if (force_screenmode_req) {
        force_screenmode_req = 0;
        DisplayID = (ULONG)INVALID_ID;
    }

    if (DisplayID == (ULONG)INVALID_ID) {
	if (AslRequestTags (ScreenRequest,
			ASLSM_TitleText, (ULONG)"Select screen display mode",
			ASLSM_InitialDisplayID,    0,
			ASLSM_InitialDisplayDepth, 8,
			ASLSM_InitialDisplayWidth, gfxvidinfo.width,
			ASLSM_InitialDisplayHeight,gfxvidinfo.height,
			ASLSM_MinWidth,            320, //currprefs.gfx_width_win,
			ASLSM_MinHeight,           200, //currprefs.gfx_height_win,
			ASLSM_DoWidth,             TRUE,
			ASLSM_DoHeight,            TRUE,
			ASLSM_DoDepth,             TRUE,
			ASLSM_DoOverscanType,      TRUE,
			ASLSM_PropertyFlags,       0,
			ASLSM_PropertyMask,        DIPF_IS_DUALPF | DIPF_IS_PF2PRI,
			TAG_DONE)) {
	    ScreenWidth  = ScreenRequest->sm_DisplayWidth;
	    ScreenHeight = ScreenRequest->sm_DisplayHeight;
	    Depth        = ScreenRequest->sm_DisplayDepth;
	    DisplayID    = ScreenRequest->sm_DisplayID;
	    OverscanType = ScreenRequest->sm_OverscanType;
	    AutoScroll   = ScreenRequest->sm_AutoScroll;
	} else
	    DisplayID = INVALID_ID;
    }
    FreeAslRequest (ScreenRequest);

    if (DisplayID == (ULONG)INVALID_ID)
	return 0;
    /* If UAESM (or forced default) was used, requester didn't populate these */
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        LONG mw = 0, mh = 0;
        if (get_mode_nominal_size(DisplayID, &mw, &mh)) {
            ScreenWidth  = mw;
            ScreenHeight = mh;
        } else {
            /* fallback: at least request UAE's size */
            ScreenWidth  = gfxvidinfo.width;
            ScreenHeight = gfxvidinfo.height;
        }
    }

#ifdef USE_CYBERGFX
    if (CyberGfxBase && IsCyberModeID (DisplayID) && (Depth > 8)) {
	use_cyb = 1;

    }
#endif
    if ((DisplayID & HAM_KEY) && !use_cyb )
	Depth = 6; /* only ham6 for the moment */
#if 0
    if(DisplayID & DIPF_IS_HAM) Depth = 6; /* only ham6 for the moment */
#endif

    /* If chosen screen is smaller than UAE display size then clip
     * display to screen size */
    if (ScreenWidth  < gfxvidinfo.width)
	gfxvidinfo.width = ScreenWidth;
    if (ScreenHeight < gfxvidinfo.height)
	gfxvidinfo.height = ScreenHeight;

    /* If chosen screen is larger, than centre UAE's display */
    if (ScreenWidth > gfxvidinfo.width)
	XOffset = (ScreenWidth - gfxvidinfo.width) / 2;
    if (ScreenHeight > gfxvidinfo.height)
	YOffset = (ScreenHeight - gfxvidinfo.height) / 2;

    write_log ("DISPLAY: OpenScreenTags DisplayID=0x%08lx %ldx%ldx%ld\n",
               (unsigned long)DisplayID, (long)ScreenWidth, (long)ScreenHeight, (long)Depth);
    S = OpenScreenTags (NULL,
			SA_DisplayID,			 DisplayID,
			SA_Width,			 ScreenWidth,
			SA_Height,			 ScreenHeight,
			SA_Depth,			 Depth,
			SA_Overscan,			 OverscanType,
			SA_AutoScroll,			 AutoScroll,
			SA_ShowTitle,			 FALSE,
			SA_Quiet,			 TRUE,
			SA_Behind,			 TRUE,
			SA_PubName,			 (ULONG)get_uae_pubname(),
			/* v39 stuff here: */
			(os39 ? SA_BackFill : TAG_DONE), (ULONG)LAYERS_NOBACKFILL,
			SA_SharePens,			 TRUE,
			SA_Exclusive,			 (use_cyb ? TRUE : FALSE),
			SA_Draggable,			 (use_cyb ? FALSE : TRUE),
			/* SA_Interleaved only for planar screens — crashes some CGX drivers */
			(use_cyb ? TAG_IGNORE : SA_Interleaved), TRUE,
			TAG_DONE);
    if (!S) {
	gui_message ("Unable to open the requested screen.\n");
	return 0;
    }

    CM           =  S->ViewPort.ColorMap;
    is_halfbrite = (S->ViewPort.Modes & EXTRA_HALFBRITE);
    is_ham       = (S->ViewPort.Modes & HAM);

    W = OpenWindowTags (NULL,
			WA_Width,		S->Width,
			WA_Height,		S->Height,
			WA_CustomScreen,	(ULONG)S,
			WA_Backdrop,		TRUE,
			WA_Borderless,		TRUE,
			WA_RMBTrap,		TRUE,
			WA_Activate,		TRUE,
			WA_ReportMouse,		TRUE,
			WA_IDCMP,		IDCMP_MOUSEBUTTONS
					      | IDCMP_RAWKEY
					      | IDCMP_DISKINSERTED
					      | IDCMP_DISKREMOVED
					      | IDCMP_ACTIVEWINDOW
					      | IDCMP_INACTIVEWINDOW
					      | IDCMP_MOUSEMOVE
					      | IDCMP_DELTAMOVE,
			(os39 ? WA_BackFill : TAG_IGNORE),   (ULONG) LAYERS_NOBACKFILL,
			TAG_DONE);

    if(!W) {
	write_log ("AMIGFX: Unable to open the window.\n");
	CloseScreen (S);
	S  = NULL;
	RP = NULL;
	CM = NULL;
	return 0;
    }

    hide_pointer (W);

    RP = W->RPort; /* &S->Rastport if screen is not public */

    PubScreenStatus (S, 0);

    write_log ("AMIGFX: Using screenmode: 0x%lx:%ld (%lu:%ld)\n",
	DisplayID, Depth, DisplayID, Depth);

    return 1;
}

/****************************************************************************/

int graphics_setup (void)
{
    if (((struct ExecBase *)SysBase)->LibNode.lib_Version < 36) {
	write_log ("UAE needs OS 2.0+ !\n");
	return 0;
    }
    os39 = (((struct ExecBase *)SysBase)->LibNode.lib_Version >= 39);

    atexit (graphics_leave);

    IntuitionBase = (void*) OpenLibrary ("intuition.library", 0L);
    if (!IntuitionBase) {
	write_log ("No intuition.library ?\n");
	return 0;
    } else {
#ifdef __amigaos4__
	IIntuition = (struct IntuitionIFace *) GetInterface ((struct Library *) IntuitionBase, "main", 1, NULL);
	if (!IIntuition) {
	    CloseLibrary ((struct Library *) IntuitionBase);
	    IntuitionBase = 0;
	    return 0;
	}
#endif
    }

    GfxBase = (void*) OpenLibrary ("graphics.library", 0L);
    if (!GfxBase) {
	write_log ("No graphics.library ?\n");
	return 0;
    } else {
#ifdef __amigaos4__
	IGraphics = (struct GraphicsIFace *) GetInterface ((struct Library *) GfxBase, "main", 1, NULL);
	if (!IGraphics) {
	    CloseLibrary ((struct Library *) GfxBase);
	    GfxBase = 0;
	    return 0;
	}
#endif
    }

    LayersBase = OpenLibrary ("layers.library", 0L);
    if (!LayersBase) {
	write_log ("No layers.library\n");
	return 0;
    } else {
#ifdef __amigaos4__
	ILayers = (struct LayersIFace *) GetInterface (LayersBase, "main", 1, NULL);
	if (!ILayers) {
	    CloseLibrary (LayersBase);
	    LayersBase = 0;
	    return 0;
	}
#endif
    }

#ifdef USE_CYBERGFX
    if (!CyberGfxBase) {
        CyberGfxBase = OpenLibrary ("cybergraphics.library", 40);
#ifdef __amigaos4__
        if (CyberGfxBase) {
	   ICyberGfx = (struct CyberGfxIFace *) GetInterface (CyberGfxBase, "main", 1, NULL);
           if (!ICyberGfx) {
	       CloseLibrary (CyberGfxBase);
	       CyberGfxBase = 0;
	   }
	}
#endif
    }
#endif
    init_pointer ();

    /* Open input.device for pointer warping during mouse grab */
    input_mp = CreateMsgPort ();
    if (input_mp) {
        input_req = (struct IOStdReq *) CreateIORequest (input_mp, sizeof (struct IOStdReq));
        if (input_req) {
            if (OpenDevice ("input.device", 0, (struct IORequest *)input_req, 0) == 0)
                input_open = 1;
            else {
                DeleteIORequest ((struct IORequest *)input_req);
                input_req = NULL;
            }
        }
        if (!input_open) {
            DeleteMsgPort (input_mp);
            input_mp = NULL;
        }
    }
    if (!input_open)
        write_log ("MOUSE: input.device unavailable, pointer warp disabled\n");


    initpseudodevices ();
    ami_show_startup_splash();
    return 1;
}

/****************************************************************************/

static struct Window *saved_prWindowPtr;

static void set_prWindowPtr (struct Window *w)
{
   struct Process *self = (struct Process *) FindTask (NULL);

   if (!saved_prWindowPtr)
	saved_prWindowPtr = self->pr_WindowPtr;
   self->pr_WindowPtr = w;
}

static void restore_prWindowPtr (void)
{
   struct Process *self = (struct Process *) FindTask (NULL);

   if (saved_prWindowPtr)
	self->pr_WindowPtr = saved_prWindowPtr;
}

/****************************************************************************/

#ifdef USE_CYBERGFX
# ifdef USE_CYBERGFX_V41
/* Allocate and set-up off-screen buffer for rendering Amiga display to
 * when using CGX V41 or better
 *
 * gfxinfo - the buffer description (which gets filled in by this routine)
 * rp      - the Rastport this buffer will be blitted to
 */
static APTR setup_cgx41_buffer (struct vidbuf_description *gfxinfo, const struct RastPort *rp)
{
    int bytes_per_pixel = GetCyberMapAttr (rp->BitMap, CYBRMATTR_BPPIX);
    int bytes_per_row;
    APTR buffer;

    if (scale_x > gfxinfo->width || scale_y > gfxinfo->height) {
        /* Scaling active: buffer is sized for the native render resolution.
         * ScalePixelArray accepts an explicit source modulo so we don't need
         * to match the destination bitmap stride. */
        bytes_per_row = gfxinfo->width * bytes_per_pixel;
    } else {
        /* No scaling: use destination bitmap stride as before.
         * WritePixelArray (RECTFMT_RAW) requires src and dst modulos to match. */
        bytes_per_row = GetCyberMapAttr (rp->BitMap, CYBRMATTR_XMOD);
    }

    buffer = AllocVec (bytes_per_row * gfxinfo->height, MEMF_ANY);

    if (buffer) {
        gfxinfo->bufmem      = buffer;
        gfxinfo->pixbytes    = bytes_per_pixel;
        gfxinfo->rowbytes    = bytes_per_row;

        gfxinfo->flush_line  = flush_line_cgx_v41;
        gfxinfo->flush_block = flush_block_cgx_v41;
    }
    return buffer;
}
# else
/* Allocate and set-up off-screen buffer for rendering Amiga display to
 * when using pre-CGX V41.
 *
 * gfxinfo - the buffer description (which gets filled in by this routine)
 * rp      - the Rastport this buffer will be blitted to
 */
static APTR setup_cgx_buffer (struct vidbuf_description *gfxinfo, const struct RastPort *rp)
{
    int pixfmt   = GetCyberMapAttr (rp->BitMap, CYBRMATTR_PIXFMT);
    int bitdepth = RPDepth (RP);
    struct BitMap *bitmap;

    write_log ("setup_cgx_buffer: allocating %dx%d bitmap\n", gfxinfo->width, gfxinfo->height);
    bitmap = myAllocBitMap (gfxinfo->width, gfxinfo->height + 1,
			    bitdepth,
			    (pixfmt << 24) | BMF_SPECIALFMT | BMF_MINPLANES,
			    rp->BitMap);

    if (bitmap) {
	gfxinfo->bufmem   = (char *) GetCyberMapAttr (bitmap, CYBRMATTR_DISPADR);
	gfxinfo->rowbytes =          GetCyberMapAttr (bitmap, CYBRMATTR_XMOD);
	gfxinfo->pixbytes =          GetCyberMapAttr (bitmap, CYBRMATTR_BPPIX);
	gfxinfo->flush_line  = flush_line_cgx;
	gfxinfo->flush_block = flush_block_cgx;
    }

    return bitmap;
}
# endif
#endif

int graphics_init (void)
{
    int i, bitdepth;
    /* e.g., in graphics_init() after set_default_hotkeys(...) */
    savestate_initsave("uae_quick.uss", 2);   // 2 = uncompressed
    use_delta_buffer = 0;
    need_dither = 0;
    use_cyb = 0;

/* We'll ignore color_mode for now.
    if (currprefs.color_mode > 5) {
        write_log ("Bad color mode selected. Using default.\n");
        currprefs.color_mode = 0;
    }
*/

    gfxvidinfo.width  = currprefs.gfx_width_win;
    gfxvidinfo.height = currprefs.gfx_height_win;

    if (gfxvidinfo.width < 320)
	gfxvidinfo.width = 320;
    if (!currprefs.gfx_correct_aspect && (gfxvidinfo.width < 64))
	gfxvidinfo.width = 200;

    gfxvidinfo.width += 7;
    gfxvidinfo.width &= ~7;

    switch (currprefs.amiga_screen_type) {
	case UAESCREENTYPE_ASK:
	    if (setup_userscreen ())
		break;
	    write_log ("Trying on public screen...\n");
	    /* fall trough */
	case UAESCREENTYPE_PUBLIC:
	    is_halfbrite = 0;
	    if (setup_publicscreen ()) {
		usepub = 1;
		break;
	    }
	    write_log ("Trying on custom screen...\n");
	    /* fall trough */
	case UAESCREENTYPE_CUSTOM:
	default:
	    if (!setup_customscreen ())
		return 0;
	    break;
    }

    set_prWindowPtr (W);

    Line = AllocVec ((gfxvidinfo.width + 15) & ~15, MEMF_ANY | MEMF_PUBLIC);
    if (!Line) {
	write_log ("Unable to allocate raster buffer.\n");
	return 0;
    }
    BitMap = myAllocBitMap (gfxvidinfo.width, 1, 8, BMF_CLEAR | BMF_MINPLANES, RP->BitMap);
    if (!BitMap) {
	write_log ("Unable to allocate BitMap.\n");
	return 0;
    }
    TempRPort = AllocVec (sizeof (struct RastPort), MEMF_ANY | MEMF_PUBLIC);
    if (!TempRPort) {
	write_log ("Unable to allocate RastPort.\n");
	return 0;
    }
    CopyMem (RP, TempRPort, sizeof (struct RastPort));
    TempRPort->Layer  = NULL;
    TempRPort->BitMap = BitMap;

    if (usepub)
	set_title ();

    bitdepth = RPDepth (RP);

    gfxvidinfo.emergmem = 0;
    gfxvidinfo.linemem  = 0;

#ifdef USE_CYBERGFX
    if (use_cyb) {
	/*
	 * If using P96/CGX for output try to allocate on off-screen bitmap
	 * as the display buffer
	 *
	 * We do this now, so if it fails we can easily fall back on using
	 * graphics.library and palette-based rendering.
	 */


# ifdef USE_CYBERGFX_V41
	CybBuffer = setup_cgx41_buffer (&gfxvidinfo, RP);

	if (!CybBuffer) {
# else
	CybBitMap = setup_cgx_buffer (&gfxvidinfo, RP);

	if (!CybBitMap) {
# endif
	    /*
	     * Failed to allocate bitmap - we need to fall back on gfx.lib rendering
	     */
	    gfxvidinfo.bufmem = NULL;
	    use_cyb = 0;
	    if (bitdepth > 8) {
		bitdepth = 8;
		write_log ("AMIGFX: Failed to allocate off-screen buffer - falling back on 8-bit mode\n");
	    }
	}
    }
#endif

    if (is_ham) {
	/* ham 6 */
	use_delta_buffer       = 0; /* needless as the line must be fully recomputed */
	need_dither            = 0;
	gfxvidinfo.pixbytes    = 2;
	gfxvidinfo.flush_line  = flush_line_ham;
	gfxvidinfo.flush_block = flush_block_ham;
    } else if (bitdepth <= 8) {
	/* chunk2planar is slow so we define use_delta_buffer for all modes */
	use_delta_buffer       = 1;
	need_dither            = currprefs.amiga_use_dither || (bitdepth <= 1);
	gfxvidinfo.pixbytes    = need_dither ? 2 : 1;
	gfxvidinfo.flush_line  = need_dither ? flush_line_planar_dither  : flush_line_planar_nodither;
	gfxvidinfo.flush_block = need_dither ? flush_block_planar_dither : flush_block_planar_nodither;
    }

    gfxvidinfo.flush_clear_screen = flush_clear_screen_gfxlib;
    gfxvidinfo.flush_screen       = dummy_flush_screen;
    gfxvidinfo.lockscr            = dummy_lock;
    gfxvidinfo.unlockscr          = dummy_unlock;

    if (!use_cyb) {
	/*
	 * We're not using GGX/P96 for output, so allocate a dumb
	 * display buffer
	 */
	gfxvidinfo.rowbytes = gfxvidinfo.pixbytes * gfxvidinfo.width;
	gfxvidinfo.bufmem   = (uae_u8 *) calloc (gfxvidinfo.rowbytes, gfxvidinfo.height + 1);
	/*									       ^^^ */
	/*				       This is because DitherLine may read one extra row */
    }

    if (!gfxvidinfo.bufmem) {
	write_log ("AMIGFX: Not enough memory for video bufmem.\n");
	return 0;
    }


    if (use_delta_buffer) {
	oldpixbuf = (uae_u8 *) calloc (gfxvidinfo.rowbytes, gfxvidinfo.height);
	if (!oldpixbuf) {
	    write_log ("AMIGFX: Not enough memory for oldpixbuf.\n");
	    return 0;
	}
    }

    gfxvidinfo.maxblocklines = MAXBLOCKLINES_MAX;

    if (!init_colors ()) {
	write_log ("AMIGFX: Failed to init colors.\n");
	return 0;
    }

    if (!usepub)
	ScreenToFront (S);

    reset_drawing ();

    set_default_hotkeys (ami_hotkeys);
    savestate_initsave("uae_quick.uss", 0); /* compressed, single-slot quick state */
    pointer_state = DONT_KNOW;

   return 1;
}

/****************************************************************************/

void graphics_leave (void)
{
    closepseudodevices ();
    appw_exit ();

    /* Close input.device */
    if (input_open) {
        CloseDevice ((struct IORequest *)input_req);
        input_open = 0;
    }
    if (input_req)  { DeleteIORequest ((struct IORequest *)input_req); input_req = NULL; }
    if (input_mp)   { DeleteMsgPort (input_mp); input_mp = NULL; }

#ifdef USE_CYBERGFX
# ifdef USE_CYBERGFX_V41
    if (CybBuffer) {
	FreeVec (CybBuffer);
        CybBuffer = NULL;
    }
# else
    if (CybBitMap) {
	WaitBlit ();
	myFreeBitMap (CybBitMap);
	CybBitMap = NULL;
    }
# endif
#endif
    if (BitMap) {
	WaitBlit ();
	myFreeBitMap (BitMap);
	BitMap = NULL;
    }
    if (TempRPort) {
	FreeVec (TempRPort);
	TempRPort = NULL;
    }
    if (Line) {
	FreeVec (Line);
	Line = NULL;
    }
    if (CM) {
	ReleaseColors();
	CM = NULL;
    }
    if (W) {
	restore_prWindowPtr ();
	CloseWindow (W);
	W = NULL;
    }

    free_pointer ();

    if (!usepub && S) {
	if (!CloseScreen (S)) {
	    gui_message ("Please close all opened windows on UAE's screen.\n");
	    do
		Delay (50);
	    while (!CloseScreen (S));
	}
	S = NULL;
    }
    if (AslBase) {
	CloseLibrary( (void*) AslBase);
	AslBase = NULL;
    }
    if (GfxBase) {
	CloseLibrary ((void*)GfxBase);
	GfxBase = NULL;
    }
    if (LayersBase) {
	CloseLibrary (LayersBase);
	LayersBase = NULL;
    }
    if (IntuitionBase) {
	CloseLibrary ((void*)IntuitionBase);
	IntuitionBase = NULL;
    }
    if (CyberGfxBase) {
	CloseLibrary((void*)CyberGfxBase);
	CyberGfxBase = NULL;
    }
}

/****************************************************************************/

int do_inhibit_frame (int onoff)
{
    if (onoff != -1) {
	inhibit_frame = onoff ? 1 : 0;
	if (inhibit_frame)
	    write_log ("display disabled\n");
	else
	    write_log ("display enabled\n");
	set_title ();
    }
    return inhibit_frame;
}

/***************************************************************************/

void graphics_notify_state (int state)
{
}

/***************************************************************************/
#include "options.h"   /* for currprefs / changed_prefs */
extern void audio_volume (int volume);  /* from audio.c */

static void frf_adjust_volume_and_apply (int dir)
{
    int step = 5; /* percent per press */
    int v = currprefs.sound_volume;

    if (v < 0) v = 0;
    if (v > 100) v = 100;

    v += dir * step;

    if (v < 0) v = 0;
    if (v > 100) v = 100;

    /* Keep prefs consistent */
    currprefs.sound_volume = v;
    changed_prefs.sound_volume = v;

    /* CRITICAL: apply immediately */
    audio_volume (v);

    write_log ("VOLUME: %d%%\n", v);
}

void handle_events(void)
{
    struct IntuiMessage *msg;
    int dmx, dmy, mx, my, class, code, qualifier;

    /* Act on all deferred display changes — must be done here, outside the
     * IntuiMessage loop, so CloseWindow is never called mid-message. */
    if (pending_toggle_fs) {
        pending_toggle_fs = 0;
        do_toggle_fullscreen ();
    } else if (pending_toggle_res) {
        pending_toggle_res = 0;
        do_toggle_lores ();
    } else if (pending_screenmode) {
        pending_screenmode = 0;
        close_display ();
        currprefs.amiga_screen_type     = UAESCREENTYPE_ASK;
        changed_prefs.amiga_screen_type = UAESCREENTYPE_ASK;
        if (!open_display ()) {
            write_log ("DISPLAY: screenmode requester failed, reverting to public screen\n");
            currprefs.amiga_screen_type     = UAESCREENTYPE_PUBLIC;
            changed_prefs.amiga_screen_type = UAESCREENTYPE_PUBLIC;
            open_display ();
        }
    } else if (pending_resize) {
        pending_resize = 0;
        close_display ();
        open_display ();
#ifdef PICASSO96
        p96_invalidate_all ();
#endif
    }
#ifdef PICASSO96
    else if (pending_p96) {
        pending_p96 = 0;
        do_p96_display ();
    }
    p96_flush ();                       /* the card's changed lines to the window */
#endif

    /* this function is called at each frame, so: */
    ++frame_num;       /* increase frame counter */
    #if 0
    save_frame();      /* possibly save frame    */
    #endif

    #ifdef DEBUGGER
    /*
     * This is a hack to simulate ^C as it seems that break_handler
     * is lost when system() is called.
     */
    if (SetSignal (0L, SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D) &
        (SIGBREAKF_CTRL_C|SIGBREAKF_CTRL_D)) {
        activate_debugger ();
        }
        #endif

        while (W && (msg = (struct IntuiMessage*) GetMsg (W->UserPort))) {
            class     = msg->Class;
            code      = msg->Code;
            dmx       = msg->MouseX;
            dmy       = msg->MouseY;
            mx        = msg->IDCMPWindow->MouseX; /* absolute pointer coords */
            my        = msg->IDCMPWindow->MouseY; /* relative to window      */
            qualifier = msg->Qualifier;

            ReplyMsg ((struct Message*)msg);

            switch (class) {

                case IDCMP_NEWSIZE:
                    do_inhibit_frame ((W->Flags & WFLG_ZOOMED) ? 1 : 0);
                    if (usepub && !(W->Flags & WFLG_ZOOMED)) {
                        int new_w = W->Width  - W->BorderLeft - W->BorderRight;
                        int new_h = W->Height - W->BorderTop  - W->BorderBottom;
                        if (new_w != scale_x || new_h != scale_y) {
                            /* Defer close/open to top of next handle_events —
                             * calling it here while processing IntuiMessages
                             * closes the window mid-loop and freezes. */
                            display_w = new_w;
                            display_h = new_h;
                            pending_resize = 1;
                        }
                    }
                    break;

                case IDCMP_REFRESHWINDOW:
#ifdef PICASSO96
                    if (screen_is_picasso) {
                        BeginRefresh (W);
                        EndRefresh (W, TRUE);
                        p96_invalidate_all ();  /* redrawn at the frame's end */
                        break;
                    }
#endif
                    if (use_delta_buffer) {
                        /* hack: this forces refresh */
                        uae_u8 *ptr = oldpixbuf;
                        int i, len = gfxvidinfo.width * gfxvidinfo.pixbytes;
                        for (i = 0; i < currprefs.gfx_height_win; ++i) {
                            ptr[0] ^= 255;
                            ptr[len - 1] ^= 255;
                            ptr += gfxvidinfo.rowbytes;
                        }
                    }
                    BeginRefresh (W);
                    flush_block (0, currprefs.gfx_height_win - 1);
                    EndRefresh (W, TRUE);
                    break;

                case IDCMP_CLOSEWINDOW:
                    uae_quit ();
                    break;

                    /* === FRF: RAWKEY + Ctrl+LAlt gated hotkeys + 3-char quick save/load === */
                    case IDCMP_RAWKEY:
                    {
                        int keycode = code & 127;           /* scancode 0..127 */
                        int state   = (code & 128) ? 0 : 1; /* 1 = key down, 0 = key up */
                        int ievent  = 0;
                        int handled = 0;

                        /* Ignore autorepeat to keep sequences clean */
                        if (qualifier & IEQUALIFIER_REPEAT)
                            break;

                        /* ------------------------------------------------------------
                         * FRF: after quick save/load, ignore hotkeys until chord released
                         * This prevents "stuck Ctrl+LAlt" making next key (e.g. F1) act as hotkey.
                         * ------------------------------------------------------------ */
                        if (frf_block_hotkeys_until_release) {
                            /* Let the matcher unwind its internal state anyway */
                            (void)match_hotkey_sequence(keycode, state);

                            /* Clear block once Ctrl+LAlt are no longer held */
                            if ((qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT)) == 0)
                                frf_block_hotkeys_until_release = 0;

                            /* Always pass through as normal Amiga input while blocked */
                            inputdevice_do_keyboard(keycode, state);
                            break;
                        }

                        /* If we are currently typing a 3-char quick code, feed chars first */
                        if (quickcode_mode) {
                            /* CRITICAL: keep feeding matcher so hotkey chord state can't stick */
                            (void)match_hotkey_sequence(keycode, state);

                            if (state)
                                quickcode_feed_key(keycode);
                            break;
                        }

                        /* Gate ALL local hotkeys behind Ctrl+LAlt chord */
                        {
                            const int hotkey_chord =
                            (qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT))
                            == (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT);

                            if (state && hotkey_chord) {

                                /* HUD toggle: Ctrl+LAlt+H */
                                if (keycode == 0x25 /* 'H' */) {
                                    hotkey_toggle_hud();
                                    handled = 1;
                                }

                                /* GUI toggle: Ctrl+LAlt+B */
                                if (!handled && keycode == 0x35 /* 'B' */) {
                                    gui_toggle();
                                    handled = 1;
                                }

                                /* Numpad-only: Ctrl+LAlt + KP7/KP8 volume, KP+/- frameskip */
                                if (!handled && (qualifier & IEQUALIFIER_NUMERICPAD)) {

                                    /* Volume: KP7 down, KP8 up (your swapped mapping) */
                                    if (keycode == RAWKEY_NP_8) { frf_adjust_volume_and_apply(+1); handled = 1; }
                                    else if (keycode == RAWKEY_NP_7) { frf_adjust_volume_and_apply(-1); handled = 1; }

                                    /* Frameskip: KP+ more, KP- less */
                                    else if (keycode == RAWKEY_NP_ADD) { hotkey_adjust_frameskip(+1); handled = 1; }
                                    else if (keycode == RAWKEY_NP_SUB) { hotkey_adjust_frameskip(-1); handled = 1; }
                                }

                                if (handled)
                                    break; /* we consumed it */
                            }
                        }

                        /* Normal hotkey handling (Ctrl+LAlt sequences, F5/F6, etc.) */
                        ievent = match_hotkey_sequence(keycode, state);

                        if (ievent) {
                            /* Start 3-char SAVE / LOAD code entry on our two special hotkeys */
                            if (state && ievent == INPUTEVENT_SPC_STATESAVEQUICK) {
                                write_log("HOTKEY: begin Quick SAVE code (Ctrl+LAlt then F5)\n");
                                quickcode_start(1);    /* 1 = save */
                                break;
                            } else if (state && ievent == INPUTEVENT_SPC_STATERESTOREQUICK) {
                                write_log("HOTKEY: begin Quick LOAD code (Ctrl+LAlt then F6)\n");
                                quickcode_start(2);    /* 2 = load */
                                break;
                            }

                            /* All other hotkeys go through as normal */
                            handle_hotkey_event(ievent, state);
                        } else {
                            /* Not a hotkey sequence: pass to keyboard device */
                            inputdevice_do_keyboard(keycode, state);
                        }

                        break;
                    }





                                        case IDCMP_MOUSEMOVE:
                                            if (mouseGrabbed && warpSkip > 0) {
                                                /* Discard deltas generated by our own warp */
                                                warpSkip--;
                                            } else {
                                                setmousestate (0, 0, dmx, 0);
                                                setmousestate (0, 1, dmy, 0);
                                            }

                                            if (mouseGrabbed && usepub) {
                                                /* Windowed grab: warp back to centre when
                                                 * pointer escapes so it never gets stuck
                                                 * at the screen edge */
                                                if (get_pointer_state (W, mx, my) == OUTSIDE_WINDOW)
                                                    warp_pointer_to_center ();
                                            } else if (usepub) {
                                                POINTER_STATE new_state = get_pointer_state (W, mx, my);
                                                if (new_state != pointer_state) {
                                                    pointer_state = new_state;
                                                    if (pointer_state == INSIDE_WINDOW)
                                                        hide_pointer (W);
                                                    else
                                                        show_pointer (W);
                                                }
                                            }
                                            break;

                                        case IDCMP_MOUSEBUTTONS:
                                            if (code == SELECTDOWN) setmousebuttonstate (0, 0, 1);
                                            if (code == SELECTUP)   setmousebuttonstate (0, 0, 0);
                                            if (code == MIDDLEDOWN) setmousebuttonstate (0, 2, 1);
                                            if (code == MIDDLEUP)   setmousebuttonstate (0, 2, 0);
                                            if (code == MENUDOWN)   setmousebuttonstate (0, 1, 1);
                                            if (code == MENUUP)     setmousebuttonstate (0, 1, 0);
                                            break;

                /* Those 2 could be of some use later. */
                case IDCMP_DISKINSERTED:
                    /*printf("diskinserted(%d)\n",code);*/
                    break;

                case IDCMP_DISKREMOVED:
                    /*printf("diskremoved(%d)\n",code);*/
                    break;

                case IDCMP_ACTIVEWINDOW:
                    /* When window regains focus, release any stuck keys. */
                    inputdevice_acquire ();
                    inputdevice_release_all_keys ();
                    reset_hotkeys ();
                    break;

                case IDCMP_INACTIVEWINDOW:
                    inputdevice_unacquire ();
                    /* Pause emulation when windowed and focus is lost.
                     * In fullscreen (custom screen) we own the display so
                     * there is nowhere else to focus — no pause needed.
                     * Disabled when UAE_NO_PAUSE_ON_FOCUS_LOSS env var is set. */
                    if (ami_no_pause_on_focus_loss < 0) {
                        const char *e = getenv(UAE_NO_PAUSE_ENV);
                        ami_no_pause_on_focus_loss = (e && e[0] != '0' && e[0] != '\0') ? 1 : 0;
                        if (ami_no_pause_on_focus_loss)
                            write_log("FOCUS: pause-on-focus-loss disabled (%s is set)\n", UAE_NO_PAUSE_ENV);
                    }
                    if (usepub && !ami_no_pause_on_focus_loss && !gui_screen_open
                        && !pending_screenmode) {
                        ami_pause_focus_loss = 1;
                        ami_pause_loop ();
                    }
                    break;

                case IDCMP_INTUITICKS:
                    /* Periodically re-apply mouse limits — Intuition can
                     * reset WA_MouseLimits on window resize or screen change */
                    grabTicks--;
                    if (grabTicks < 0) {
                        grabTicks = GRAB_TIMEOUT;
                        if (mouseGrabbed && W)
                            grab_pointer (W);
                    }
                    break;

                default:
                    write_log ("Unknown event class: %x\n", class);
                    break;
            } /* end switch(class) */
        } /* end while(GetMsg) */

        appw_events();

        /* real floppy drives: flush writes, motor off, follow disk changes */
        hostdisk_tick ();

        /* --- GUI window: pump IDCMP events every frame --- */
        gui_handle_events();

        /* --- Service GUI-requested save/load safely here (end of frame) --- */
        if (pending_quick_save && gui_state_filename[0]) {
            pending_quick_save = 0;
            savestate_initsave(gui_state_filename, 0);
            gui_state_filename[0] = '\0';
            savestate_quick(1, 1);
            write_log("QUICKSTATE: GUI save complete\n");
        }
        if (pending_quick_load && gui_state_filename[0]) {
            pending_quick_load = 0;
            savestate_initsave(gui_state_filename, 0);
            gui_state_filename[0] = '\0';
            savestate_quick(1, 0);
            write_log("QUICKSTATE: GUI load complete\n");
        }

        /* --- Auto-sync frameskip HUD with prefs (keeps number + flash live) --- */
        {
            static int last_fs = -9999;
            int fs = currprefs.gfx_framerate;   /* current frameskip setting */
            if (fs != last_fs) {
                gui_frameskip_set(fs);
                gui_frameskip_flash((fs > last_fs) ? +1 : -1); /* + => RED, - => GREEN */
                last_fs = fs;
            }
        }
} /* end handle_events */

/***************************************************************************/

int debuggable (void)
{
    return 1;
}

/***************************************************************************/

int mousehack_allowed (void)
{
    return 0;
}

/***************************************************************************/

void LED (int on)
{
}

/***************************************************************************/

/* sam: need to put all this in a separate module */

#ifdef PICASSO96
/*
 * The emulated graphics card's side of the display (uaegfx in the emulated
 * Amiga, picasso96.c in the emulator).
 *
 * picasso96.c draws the card's picture into a buffer of ours (gfx_lock_picasso)
 * in the host's pixel format, and says which lines changed (DX_Invalidate).
 * Ours is 32-bit ARGB in memory order - RGBFB_A8R8G8B8; picasso96.c then
 * offers the emulated Amiga BGRA and byte-swaps (its "argb32 hack") - and
 * 8-bit modes, whose pens it turns into ARGB through picasso_vidinfo.clut
 * (DX_SetPalette). Each frame p96_flush puts the changed lines in the window
 * with WritePixelArray (RECTFMT_ARGB), so the host needs an RTG screen
 * (cybergraphics.library: Picasso96 or CGX). While the card is shown the
 * chipset's picture is held (drawing.c: IHF_PICASSO).
 *
 * Switching between the two reopens the display at the other size, deferred
 * to handle_events like the other display changes (pending_p96).
 */

extern uae_u32 gfxmem_start, allocated_gfxmem;  /* the card's memory (memory.h) */
static uae_u32 *p96buf;                 /* the card's picture, ARGB */
static int p96buf_w, p96buf_h;          /* ... its size */
static int p96_dirty_first = -1, p96_dirty_last = -1;
static int p96_saved_dw;                /* the window was switched to the card's size */
static int p96_sample_req;              /* log the picture's colours a second from now */

static void p96_alloc (int w, int h)
{
    if (p96buf && p96buf_w == w && p96buf_h == h)
        return;
    if (p96buf)
        FreeVec (p96buf);
    p96buf = AllocVec ((ULONG) w * h * 4, MEMF_ANY | MEMF_CLEAR);
    p96buf_w = p96buf ? w : 0;
    p96buf_h = p96buf ? h : 0;
    if (!p96buf)
        write_log ("P96: no memory for a %dx%d picture\n", w, h);
}

static void p96_invalidate_all (void)
{
    if (p96buf_h > 0) {
        p96_dirty_first = 0;
        p96_dirty_last  = p96buf_h - 1;
    }
}

static unsigned long p96_invalidations;     /* for the log: changed-line reports */

void DX_Invalidate (int first, int last)
{
    if (first > last || !p96buf)
        return;
    p96_invalidations++;
    if (first < 0) first = 0;
    if (last >= p96buf_h) last = p96buf_h - 1;
    if (p96_dirty_first < 0 || first < p96_dirty_first) p96_dirty_first = first;
    if (last > p96_dirty_last) p96_dirty_last = last;
}

/* for the log (make EXTRA=-DP96_DEBUG): a look at the card's state and the
   picture, eight samples across the middle line, 1, 3 and 10 s after each
   mode switch */
static void p96_sample (void)
{
#ifdef P96_DEBUG
    static int frames = -1;
    static unsigned long inv0;
    if (p96_sample_req) {
        p96_sample_req = 0;
        frames = 0;
        inv0 = p96_invalidations;
    }
    if (frames < 0 || !p96buf)
        return;
    frames++;
    if (frames == 50 || frames == 150 || frames == 500) {
        const uae_u32 *row = p96buf + (p96buf_h / 2 + 16) * p96buf_w;
        write_log ("P96: after %d frames (%lu line reports): Address %08lx BytesPerRow %d %dx%d"
                   " virtual %dx%d at %d,%d format %d\n",
                   frames, p96_invalidations - inv0, (unsigned long) picasso96_state.Address,
                   (int) picasso96_state.BytesPerRow,
                   (int) picasso96_state.Width, (int) picasso96_state.Height,
                   (int) picasso96_state.VirtualWidth, (int) picasso96_state.VirtualHeight,
                   (int) picasso96_state.XOffset, (int) picasso96_state.YOffset,
                   (int) picasso96_state.RGBFormat);
        write_log ("P96: %dx%d samples %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                   p96buf_w, p96buf_h,
                   (unsigned long) row[1 * p96buf_w / 16], (unsigned long) row[3 * p96buf_w / 16],
                   (unsigned long) row[5 * p96buf_w / 16], (unsigned long) row[7 * p96buf_w / 16],
                   (unsigned long) row[9 * p96buf_w / 16], (unsigned long) row[11 * p96buf_w / 16],
                   (unsigned long) row[13 * p96buf_w / 16], (unsigned long) row[15 * p96buf_w / 16]);
    }
    if (frames == 500) {
        /* both pictures as raw files next to the log: ours (ARGB, p96buf_w x
           p96buf_h) and the card's framebuffer as the emulated Amiga has it */
        extern uae_u32 gfxmem_start;            /* expansion.c (memory.h) */
        char name[300], *p;
        FILE *f;
        GetVar ("EUAE_LOG", name, sizeof name - 40, 0);
        p = name + strlen (name);
        while (p > name && p[-1] != '/' && p[-1] != ':') p--;
        strcpy (p, "p96host.raw");
        if ((f = fopen (name, "wb"))) {
            fwrite (p96buf, 4, (size_t) p96buf_w * p96buf_h, f);
            fclose (f);
        }
        strcpy (p, "p96card.raw");
        if ((f = fopen (name, "wb"))) {
            fwrite (gfxmemory + (picasso96_state.Address - gfxmem_start), 1,
                    (size_t) picasso96_state.BytesPerRow * picasso96_state.Height, f);
            fclose (f);
        }
        write_log ("P96: dumped p96host.raw (%dx%d ARGB) and p96card.raw (%d bytes a row, %d rows, format %d)\n",
                   p96buf_w, p96buf_h, (int) picasso96_state.BytesPerRow, (int) picasso96_state.Height,
                   (int) picasso96_state.RGBFormat);
    }
#endif
}

/* The changed lines, straight from the card's framebuffer in the emulated
 * Amiga's memory into p96buf (ARGB): BGRA byte-swapped, 8-bit through the
 * CLUT. picasso96.c's own copies into a second buffer (extra_mem) go
 * through many partial paths written for little-endian hosts, and on this
 * one they left pixels shifted and bytes mixed; the framebuffer itself is
 * always right, and every path reports the lines it touched. */
static void p96_convert (int first, int last)
{
    uaecptr base = picasso96_state.Address;
    int bpr = picasso96_state.BytesPerRow, w = picasso96_state.Width, y;
    int fmt = (int) picasso96_state.RGBFormat;
    int bpp = picasso96_state.BytesPerPixel;
    const uae_u8 *fb;

    if (!base || !gfxmemory || bpr <= 0 || base < gfxmem_start)
        return;
    fb = gfxmemory + (base - gfxmem_start) + picasso96_state.YOffset * bpr
         + picasso96_state.XOffset * bpp;
    if (w > p96buf_w) w = p96buf_w;
    if (last >= picasso96_state.Height) last = picasso96_state.Height - 1;
    if ((uae_u32) ((fb - gfxmemory) + (last + 1) * bpr) > allocated_gfxmem)
        return;                                 /* not all of it in the card's memory */

    for (y = first; y <= last; y++) {
        const uae_u8 *s = fb + y * bpr;
        uae_u32 *d = p96buf + y * p96buf_w;
        int x;
        switch (fmt) {
            case RGBFB_CLUT:
                for (x = 0; x < w; x++)
                    d[x] = picasso_vidinfo.clut[s[x]];
                break;
            case RGBFB_B8G8R8A8:                /* the bytes B, G, R, A */
                for (x = 0; x < w; x++, s += 4)
                    d[x] = (uae_u32) s[2] << 16 | (uae_u32) s[1] << 8 | s[0];
                break;
            case RGBFB_A8R8G8B8:                /* A, R, G, B: ours already */
                memcpy (d, s, w * 4);
                break;
            default:                            /* not offered */
                break;
        }
    }
}

/* each frame: the changed lines to the window */
static void p96_flush (void)
{
    int x0, y0, w, h, first, last;

    if (screen_is_picasso)
        p96_sample ();
    if (!screen_is_picasso || !p96buf || p96_dirty_first < 0 || !W || !RP || !CyberGfxBase)
        return;
    if (usepub) {
        x0 = W->BorderLeft;
        y0 = W->BorderTop;
        w  = W->Width  - W->BorderLeft - W->BorderRight;
        h  = W->Height - W->BorderTop  - W->BorderBottom;
    } else {                            /* a screen of its own: in the middle */
        x0 = W->Width  > p96buf_w ? (W->Width  - p96buf_w) / 2 : 0;
        y0 = W->Height > p96buf_h ? (W->Height - p96buf_h) / 2 : 0;
        w  = W->Width  - x0;
        h  = W->Height - y0;
    }
    if (w > p96buf_w) w = p96buf_w;
    if (h > p96buf_h) h = p96buf_h;
    first = p96_dirty_first;
    last  = p96_dirty_last < h - 1 ? p96_dirty_last : h - 1;
    p96_dirty_first = p96_dirty_last = -1;
    if (w <= 0 || first > last)
        return;
    p96_convert (first, last);
    WritePixelArray (p96buf, 0, first, p96buf_w * 4, RP,
                     x0, y0 + first, w, last - first + 1, RECTFMT_ARGB);
}

int DX_BitsPerCannon (void)
{
    return 8;
}

/* 8-bit modes: the pens as ARGB, for picasso96.c's chunky-to-ARGB copies */
void DX_SetPalette (int start, int count)
{
    if (picasso96_state.RGBFormat != RGBFB_CHUNKY)
        return;
#ifdef P96_DEBUG
    {
        static int logged;
        if (logged < 40) {              /* the first few, for the log */
            logged++;
            write_log ("P96: palette %d-%d, first %02x%02x%02x\n", start, start + count - 1,
                       picasso96_state.CLUT[start].Red, picasso96_state.CLUT[start].Green,
                       picasso96_state.CLUT[start].Blue);
        }
    }
#endif
    while (count-- > 0 && start < 256) {
        picasso_vidinfo.clut[start] = (uae_u32) picasso96_state.CLUT[start].Red << 16
                                    | (uae_u32) picasso96_state.CLUT[start].Green << 8
                                    | (uae_u32) picasso96_state.CLUT[start].Blue;
        start++;
    }
    /* what's drawn already has the old colours: picasso96.c redraws on
       SetColorArray; whatever it doesn't, the next full refresh does */
}

void DX_SetPalette_vsync (void)
{
}

/* Fills and blits: picasso96.c has done them in the card's framebuffer
   already; returning 0 makes it just report the lines (extra_mem is 0) and
   p96_convert picks them up from there */
int DX_Fill (int dstx, int dsty, int width, int height, uae_u32 color, RGBFTYPE rgbtype)
{
    (void) dstx; (void) dsty; (void) width; (void) height; (void) color; (void) rgbtype;
    return 0;
}

int DX_Blit (int srcx, int srcy, int dstx, int dsty, int width, int height, BLIT_OPCODE opcode)
{
    (void) srcx; (void) srcy; (void) dstx; (void) dsty; (void) width; (void) height; (void) opcode;
    return 0;
}

/* the card's modes: each size as 8-bit and as 32-bit */
static void add_p96_mode (int w, int h, int *count)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (*count >= MAX_PICASSO_MODES)
            return;
        DisplayModes[*count].res.width  = w;
        DisplayModes[*count].res.height = h;
        DisplayModes[*count].depth      = i ? 4 : 1;    /* bytes a pixel */
        DisplayModes[*count].refresh    = 60;
        (*count)++;
    }
}

int DX_FillResolutions (uae_u16 *ppixel_format)
{
    static const int sizes[][2] = {
        {  640, 480 }, {  800, 600 }, { 1024, 768 }, { 1152, 864 },
        { 1280, 720 }, { 1280, 1024 }, { 1440, 900 }, { 1600, 1200 }, { 1920, 1080 }
    };
    int count = 0;
    unsigned int i;

    picasso_vidinfo.rgbformat = RGBFB_A8R8G8B8;     /* our buffer: ARGB in memory order */
    picasso_vidinfo.pixbytes  = 4;
    picasso_vidinfo.extra_mem = 0;      /* picasso96.c only reports changed lines; p96_convert
                                           copies them from the card's framebuffer itself */
    *ppixel_format = RGBFF_A8R8G8B8 | RGBFF_CHUNKY;

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        add_p96_mode (sizes[i][0], sizes[i][1], &count);
    write_log ("P96: %d modes, 8-bit and 32-bit\n", count);
    p96_alloc (640, 480);               /* a picture from the start: not every lock checks */
    return count;
}

void gfx_set_picasso_modeinfo (int w, int h, int depth, int rgbfmt)
{
    write_log ("P96: mode %dx%d, %d bits, format %d\n", w, h, depth, rgbfmt);
    picasso_vidinfo.width    = w;
    picasso_vidinfo.height   = h;
    picasso_vidinfo.depth    = depth;
    picasso_vidinfo.pixbytes = 4;               /* ours, whatever the card's */
    picasso_vidinfo.rowbytes = w * 4;
    p96_alloc (w, h);
    p96_invalidate_all ();
    p96_sample_req = 1;
    if (screen_is_picasso)
        pending_p96 = 1;                        /* the window to the new size */
}

void gfx_set_picasso_state (int on)
{
    write_log ("P96: the card's picture %s\n", on ? "on" : "off");
    if (on == screen_is_picasso)
        return;
    screen_is_picasso = on;
    if (on) {
        if (picasso_vidinfo.width > 0)
            p96_alloc (picasso_vidinfo.width, picasso_vidinfo.height);
        p96_invalidate_all ();
    }
    pending_p96 = 1;                            /* the window, at the next handle_events */
}

/* handle_events, outside the message loop: the display at the card's size,
 * or back at the chipset's */
static void do_p96_display (void)
{
    if (!usepub && W && S) {
        /* fullscreen: a screen to fit - the card's size, or back to the
           chipset's (get_displayid picks by screen_is_picasso) */
        if (!screen_is_picasso || S->Width < p96buf_w || S->Height < p96buf_h
            || S->Width > p96buf_w + 64 || S->Height > p96buf_h + 64) {
            close_display ();
            if (!open_display ())
                open_display ();
        }
        p96_invalidate_all ();
    } else if (screen_is_picasso) {
        if (usepub && W) {
            p96_saved_dw = 1;           /* the window is the card's size now */
            display_w = p96buf_w;
            display_h = p96buf_h;
            close_display ();
            if (!open_display ())
                open_display ();
        }
        p96_invalidate_all ();
    } else if (usepub && W && p96_saved_dw) {
        /* back at the chipset's size: the one the LoRes toggle keeps (0 for
           none yet: the prefs' window size) - not scale_x, which a
           close_display() on the way may have reset to 1 */
        display_w = last_display_w;
        display_h = last_display_h;
        p96_saved_dw = 0;
        close_display ();
        if (!open_display ())
            open_display ();
    }
}

uae_u8 *gfx_lock_picasso (void)
{
    picasso_vidinfo.rowbytes = p96buf_w * 4;
    return (uae_u8 *) p96buf;
}

void gfx_unlock_picasso (void)
{
}

#endif /* PICASSO96 */

/***************************************************************************/

static int led_state[5];

#define WINDOW_TITLE PACKAGE_NAME " " PACKAGE_VERSION

static void set_title (void)
{
#if 0
    static char title[80];
    static char ScreenTitle[200];

    if (!usepub)
	return;

    sprintf (title,"%sPower: [%c] Drives: [%c] [%c] [%c] [%c]",
	     inhibit_frame? WINDOW_TITLE " (PAUSED) - " : WINDOW_TITLE,
	     led_state[0] ? 'X' : ' ',
	     led_state[1] ? '0' : ' ',
	     led_state[2] ? '1' : ' ',
	     led_state[3] ? '2' : ' ',
	     led_state[4] ? '3' : ' ');

    if (!*ScreenTitle) {
	sprintf (ScreenTitle,
                 "UAE-%d.%d.%d (%s%s%s)  by FRF & Bernd Schmidt & contributors, "
                 "Amiga Port by Samuel Devulder. Updates Future Retro Fusion 2025-2026",
		  UAEMAJOR, UAEMINOR, UAESUBREV,
		  currprefs.cpu_level==0?"68000":
		  currprefs.cpu_level==1?"68010":
		  currprefs.cpu_level==2?"68020":"68020/68881",
		  currprefs.address_space_24?" 24bits":"",
		  currprefs.cpu_compatible?" compat":"");
        SetWindowTitles(W, title, ScreenTitle);
    } else SetWindowTitles(W, title, (char*)-1);
#endif

    const char *title = inhibit_frame ? WINDOW_TITLE " (Display off)" : WINDOW_TITLE;
    SetWindowTitles (W, title, (char*)-1);
}

/****************************************************************************/

void main_window_led (int led, int on)                /* is used in amigui.c */
{
#if 0
    if (led >= 0 && led <= 4)
	led_state[led] = on;
#endif
    set_title ();
}

/****************************************************************************/
/*
 * Routines for OS2.0 (code taken out of mpeg_play by Michael Balzer)
 */
static struct BitMap *myAllocBitMap(ULONG sizex, ULONG sizey, ULONG depth,
                                    ULONG flags, struct BitMap *friend_bitmap)
{
    struct BitMap *bm;

#if !defined __amigaos4__ && !defined __MORPHOS__ && !defined __AROS__
    if (!os39) {
	unsigned long extra = (depth > 8) ? depth - 8 : 0;
	bm = AllocVec (sizeof *bm + (extra * 4), MEMF_CLEAR);
	if (bm) {
	    ULONG i;
	    InitBitMap (bm, depth, sizex, sizey);
	    for (i = 0; i<depth; i++) {
		if (!(bm->Planes[i] = AllocRaster (sizex, sizey))) {
		    while (i--)
			FreeRaster (bm->Planes[i], sizex, sizey);
		    FreeVec (bm);
		    bm = 0;
		    break;
		}
	    }
	}
    } else
#endif
	bm = AllocBitMap (sizex, sizey, depth, flags, friend_bitmap);

    return bm;
}

/****************************************************************************/

static void myFreeBitMap(struct BitMap *bm)
{
#if !defined __amigaos4__ && !defined __MORPHOS__ && !defined __AROS__
    if (!os39) {
	while(bm->Depth--)
	    FreeRaster(bm->Planes[bm->Depth], bm->BytesPerRow*8, bm->Rows);
	FreeVec(bm);
    } else
#endif
	FreeBitMap (bm);

    return;
}

/****************************************************************************/
/*
 * find the best appropriate color. return -1 if none is available
 */
static LONG ObtainColor (ULONG r,ULONG g,ULONG b)
{
    int i, crgb;
    int colors;

    if (os39 && usepub && CM) {
	i = ObtainBestPen (CM, r, g, b,
			   OBP_Precision, (use_approx_color ? PRECISION_GUI
							    : PRECISION_EXACT),
			   OBP_FailIfBad, TRUE,
			   TAG_DONE);
	if (i != -1) {
	    if (maxpen<256)
		pen[maxpen++] = i;
	    else
		i = -1;
        }
        return i;
    }

    colors = is_halfbrite ? 32 : (1 << RPDepth (RP));

    /* private screen => standard allocation */
    if (!usepub) {
	if (maxpen >= colors)
	    return -1; /* no more colors available */
	if (os39)
	    SetRGB32 (&S->ViewPort, maxpen, r, g, b);
	else
	    SetRGB4 (&S->ViewPort, maxpen, r >> 28, g >> 28, b >> 28);
	return maxpen++;
    }

    /* public => find exact match */
    r >>= 28; g >>= 28; b >>= 28;
    if (use_approx_color)
	return get_nearest_color (r, g, b);
    crgb = (r << 8) | (g << 4) | b;
    for (i = 0; i < colors; i++ ) {
	int rgb = GetRGB4 (CM, i);
	if (rgb == crgb)
	    return i;
    }
    return -1;
}

/****************************************************************************/
/*
 * free a color entry
 */
static void ReleaseColors(void)
{
    if (os39 && usepub && CM)
	while (maxpen > 0)
	    ReleasePen (CM, pen[--maxpen]);
    else
	maxpen = 0;
}

/****************************************************************************/

static int DoSizeWindow (struct Window *W, int wi, int he)
{
    register int x,y;
    int ret = 1;

    wi += W->BorderRight + W->BorderLeft;
    he += W->BorderBottom + W->BorderTop;
    x   = W->LeftEdge;
    y   = W->TopEdge;

    if (x + wi >= W->WScreen->Width)  x = W->WScreen->Width  - wi;
    if (y + he >= W->WScreen->Height) y = W->WScreen->Height - he;

    if (x < 0 || y < 0) {
	write_log ("Working screen too small to open window (%dx%d).\n", wi, he);
	if (x < 0) {
	    x = 0;
	    wi = W->WScreen->Width;
	}
	if (y < 0) {
	    y = 0;
	    he = W->WScreen->Height;
	}
	ret = 0;
    }

    x  -= W->LeftEdge;
    y  -= W->TopEdge;
    wi -= W->Width;
    he -= W->Height;

    if (x | y)	 MoveWindow (W, x, y);
    if (wi | he) SizeWindow (W, wi, he);

    return ret;
}

/****************************************************************************/
/* Here lies an algorithm to convert a 12bits truecolor buffer into a HAM
 * buffer. That algorithm is quite fast and if you study it closely, you'll
 * understand why there is no need for MMX cpu to subtract three numbers in
 * the same time. I can think of a quicker algorithm but it'll need 4096*4096
 * = 1<<24 = 16Mb of memory. That's why I'm quite proud of this one which
 * only need roughly 64Kb (could be reduced down to 40Kb, but it's not
 * worth as I use cidx as a buffer which is 128Kb long)..
 ****************************************************************************/

static int dist4 (LONG rgb1, LONG rgb2) /* computes distance very quickly */
{
    int d = 0, t;
    t = (rgb1&0xF00)-(rgb2&0xF00); t>>=8; if (t<0) d -= t; else d += t;
    t = (rgb1&0x0F0)-(rgb2&0x0F0); t>>=4; if (t<0) d -= t; else d += t;
    t = (rgb1&0x00F)-(rgb2&0x00F); t>>=0; if (t<0) d -= t; else d += t;
#if 0
    t = rgb1^rgb2;
    if(t&15) ++d; t>>=4;
    if(t&15) ++d; t>>=4;
    if(t&15) ++d;
#endif
    return d;
}

#define d_dst (00000+(UBYTE*)cidx) /* let's use cidx as a buffer */
#define d_cmd (16384+(UBYTE*)cidx)
#define h_buf (32768+(UBYTE*)cidx)

static int init_ham (void)
{
    int i,t,RGB;

    /* try direct color first */
    for (RGB = 0; RGB < 4096; ++RGB) {
	int c,d;
	c = d = 50;
	for (i = 0; i < 16; ++i) {
	    t = dist4 (i*0x111, RGB);
	    if (t<d) {
		d = t;
		c = i;
	    }
	}
	i = (RGB & 0x00F) | ((RGB & 0x0F0) << 1) | ((RGB & 0xF00) << 2);
	d_dst[i] = (d << 2) | 3; /* the "|3" is a trick to speedup comparison */
	d_cmd[i] = c;		 /* in the conversion process */
    }
    /* then hold & modify */
    for (i = 0; i < 32768; ++i) {
	int dr, dg, db, d, c;
	dr = (i>>10) & 0x1F; dr -= 0x10; if (dr < 0) dr = -dr;
	dg = (i>>5)  & 0x1F; dg -= 0x10; if (dg < 0) dg = -dg;
	db = (i>>0)  & 0x1F; db -= 0x10; if (db < 0) db = -db;
	c  = 0; d = 50;
	t = dist4 (0,  0*256 + dg*16 + db); if (t < d) {d = t; c = 0;}
	t = dist4 (0, dr*256 +  0*16 + db); if (t < d) {d = t; c = 1;}
	t = dist4 (0, dr*256 + dg*16 +  0); if (t < d) {d = t; c = 2;}
	h_buf[i] = (d<<2) | c;
    }
    return 1;
}

/* great algorithm: convert trucolor into ham using precalc buffers */
#undef USE_BITFIELDS
static void ham_conv (UWORD *src, UBYTE *buf, UWORD len)
{
    /* A good compiler (ie. gcc :) will use bfext/bfins instructions */
#ifdef __SASC
    union { struct { unsigned int _:17, r:5, g:5, b:5; } _;
	    int all;} rgb, RGB;
#else
    union { struct { ULONG _:17,r:5,g:5,b:5;} _; ULONG all;} rgb, RGB;
#endif
    rgb.all = 0;
    while(len--) {
        UBYTE c,t;
        RGB.all = *src++;
        c = d_cmd[RGB.all];
        /* cowabonga! */
        t = h_buf[16912 + RGB.all - rgb.all];
#ifndef USE_BITFIELDS
        if(t<=d_dst[RGB.all]) {
	    static int ht[]={32+10,48+5,16+0}; ULONG m;
	    t &= 3; m = 0x1F<<(ht[t]&15);
            m = ~m; rgb.all &= m;
            m = ~m; m &= RGB.all;rgb.all |= m;
	    m >>= ht[t]&15;
	    c = (ht[t]&~15) | m;
        } else {
	    rgb.all = c;
	    rgb.all <<= 5; rgb.all |= c;
	    rgb.all <<= 5; rgb.all |= c;
        }
#else
        if(t<=d_dst[RGB.all]) {
            t&=3;
            if(!t)        {c = 32; c |= (rgb._.r = RGB._.r);}
            else {--t; if(!t) {c = 48; c |= (rgb._.g = RGB._.g);}
            else              {c = 16; c |= (rgb._.b = RGB._.b);} }
        } else rgb._.r = rgb._.g = rgb._.b = c;
#endif
        *buf++ = c;
    }
}

/****************************************************************************/

int check_prefs_changed_gfx (void)
{
    if (currprefs.gfx_lores != changed_prefs.gfx_lores) {
        currprefs.gfx_lores = changed_prefs.gfx_lores;
        return 1;
    }
    return 0;
}

/****************************************************************************/

void toggle_mousegrab (void)
{
    mouseGrabbed = 1 - mouseGrabbed;
    grabTicks    = GRAB_TIMEOUT;
    warpSkip     = 0;
    write_log ("MOUSE: grab %s\n", mouseGrabbed ? "ON" : "OFF");
    if (W) {
        grab_pointer (W);
        if (mouseGrabbed && usepub)
            warp_pointer_to_center ();
    }
}

int is_fullscreen (void)
{
    return (currprefs.amiga_screen_type != UAESCREENTYPE_PUBLIC);
}

int is_vsync (void)
{
    return 0;
}

/* Close only screen/window and graphics buffers - never touches libraries */
static void close_display (void)
{
#ifdef USE_CYBERGFX
    if (CybBuffer)  { FreeVec (CybBuffer);  CybBuffer = NULL; }
    if (CybBitMap) { WaitBlit (); myFreeBitMap (CybBitMap); CybBitMap = NULL; }
    if (CpuBuffer) { FreeVec (CpuBuffer); CpuBuffer = NULL; CpuBufRowbytes = 0; }
    if (ScaleBitMap) { WaitBlit (); myFreeBitMap (ScaleBitMap); ScaleBitMap = NULL; }
    if (ScaleBuffer) { FreeVec (ScaleBuffer); ScaleBuffer = NULL; ScaleBufRowbytes = 0; }
#endif
    if (oldpixbuf)  { free (oldpixbuf);  oldpixbuf  = NULL; }
    if (BitMap)     { WaitBlit (); myFreeBitMap (BitMap); BitMap = NULL; }
    if (TempRPort)  { FreeVec (TempRPort); TempRPort = NULL; }
    if (Line)       { FreeVec (Line);      Line      = NULL; }

    /* Release palette pens BEFORE closing window/screen and BEFORE
     * clearing usepub/CM — ReleaseColors() needs both valid to call
     * ReleasePen() correctly on a public screen. */
    ReleaseColors ();
    maxpen           = 0;
    get_color_failed = 0;
    memset (pen, 0, sizeof (pen));
    /* Note: do NOT clear xcolors[] here — init_colors() always rebuilds
     * it fully, and clearing it causes one frame of corrupt colours. */
    scale_x = 1;
    scale_y = 1;

    if (W) {
        struct IntuiMessage *imsg;
        /* Release mouse grab before closing — restores pointer limits */
        if (mouseGrabbed) {
            mouseGrabbed = 0;
            grab_pointer (W);
        }
        restore_prWindowPtr ();
        appw_exit ();
        /* Drain all pending messages from the window port before closing.
         * Intuition may have queued messages we haven't processed yet.
         * Calling CloseWindow with unprocessed messages corrupts the port. */
        Forbid ();
        while ((imsg = (struct IntuiMessage *) GetMsg (W->UserPort)) != NULL)
            ReplyMsg ((struct Message *) imsg);
        CloseWindow (W);
        Permit ();
        W = NULL;
    }
    CM = NULL;
    free_pointer ();
    if (!usepub && S) {
        if (!CloseScreen (S)) {
            do Delay (50); while (!CloseScreen (S));
        }
        S = NULL;
    } else if (usepub && S) {
        UnlockPubScreen (NULL, S);
        S = NULL;
    }
    usepub  = 0;
    use_cyb = 0;
    RP      = NULL;
    XOffset = 0;
    YOffset = 0;

    /* Tell drawing subsystem its color tables are invalid */
    notice_screen_contents_lost ();
}

/* Open screen/window and allocate buffers using current prefs.
 * Libraries must already be open (i.e. graphics_setup() already ran). */
static int open_display (void)
{
    int bitdepth;
    int save_w, save_h, pub_ok, bpp;

    use_cyb  = 0;
    usepub   = 0;
    XOffset  = 0;
    YOffset  = 0;

    /* display_w/h is the requested window size (may be larger for stretching).
     * Defaults to native render size if not set - or the card's picture
     * size while it shows (coming back from fullscreen, say). */
#ifdef PICASSO96
    if (screen_is_picasso && display_w <= 0 && p96buf_w > 0
        && currprefs.amiga_screen_type == UAESCREENTYPE_PUBLIC) {
        display_w    = p96buf_w;
        display_h    = p96buf_h;
        p96_saved_dw = 1;
    }
#endif
    if (display_w <= 0) display_w = currprefs.gfx_width_win;
    if (display_h <= 0) display_h = currprefs.gfx_height_win;

    /* Native render resolution — what the Amiga actually draws into */
    gfxvidinfo.width  = currprefs.gfx_width_win;
    gfxvidinfo.height = currprefs.gfx_height_win;
    if (gfxvidinfo.width < 320)
        gfxvidinfo.width = 320;
    if (!currprefs.gfx_correct_aspect && gfxvidinfo.width < 64)
        gfxvidinfo.width = 200;
    gfxvidinfo.width += 7;
    gfxvidinfo.width &= ~7;

    switch (currprefs.amiga_screen_type) {
        case UAESCREENTYPE_ASK:
            /* Fullscreen: setup_userscreen uses get_displayid for screen mode.
             * Clear display_w/h so no scaling is attempted in fullscreen. */
            display_w = 0;
            display_h = 0;
            if (setup_userscreen ()) break;
            write_log ("open_display: userscreen failed, trying public\n");
            /* fall through */
        case UAESCREENTYPE_PUBLIC:
            /* setup_publicscreen reads display_w/h directly and opens the
             * window at that size, keeping gfxvidinfo at render resolution. */
            is_halfbrite = 0;
            if (setup_publicscreen ()) { usepub = 1; break; }
            write_log ("open_display: public screen failed, trying custom\n");
            /* fall through */
        case UAESCREENTYPE_CUSTOM:
        default:
            display_w = 0;
            display_h = 0;
            if (!setup_customscreen ()) return 0;
            break;
    }

    set_prWindowPtr (W);

    Line = AllocVec ((gfxvidinfo.width + 15) & ~15, MEMF_ANY | MEMF_PUBLIC);
    if (!Line) return 0;
    BitMap = myAllocBitMap (gfxvidinfo.width, 1, 8, BMF_CLEAR | BMF_MINPLANES, RP->BitMap);
    if (!BitMap) return 0;
    TempRPort = AllocVec (sizeof (struct RastPort), MEMF_ANY | MEMF_PUBLIC);
    if (!TempRPort) return 0;
    CopyMem (RP, TempRPort, sizeof (struct RastPort));
    TempRPort->Layer  = NULL;
    TempRPort->BitMap = BitMap;

    if (usepub) set_title ();

    bitdepth = RPDepth (RP);
    gfxvidinfo.emergmem = 0;
    gfxvidinfo.linemem  = 0;

    /* Scale factors: fullscreen never scales (display == render).
     * Windowed: display_w/h may be larger than render for stretching. */
    if (!usepub) {
        /* Fullscreen — always match render size, no scaling */
        scale_x = gfxvidinfo.width;
        scale_y = gfxvidinfo.height;
    } else {
        scale_x = (display_w > 0) ? display_w : gfxvidinfo.width;
        scale_y = (display_h > 0) ? display_h : gfxvidinfo.height;
    }
    display_w = 0;
    display_h = 0;
    write_log ("open_display: usepub=%d gfxvidinfo=%dx%d scale=%dx%d lores=%d\n",
               usepub, gfxvidinfo.width, gfxvidinfo.height,
               scale_x, scale_y, currprefs.gfx_lores);

    write_log ("open_display: render %dx%d -> display %dx%d%s\n",
               gfxvidinfo.width, gfxvidinfo.height, scale_x, scale_y,
               (scale_x != gfxvidinfo.width || scale_y != gfxvidinfo.height) ? " (scaled)" : "");

#ifdef USE_CYBERGFX
    if (use_cyb) {
# ifdef USE_CYBERGFX_V41
        CybBuffer = setup_cgx41_buffer (&gfxvidinfo, RP);
        if (!CybBuffer) {
# else
        CybBitMap = setup_cgx_buffer (&gfxvidinfo, RP);
        if (!CybBitMap) {
# endif
            gfxvidinfo.bufmem = NULL;
            use_cyb = 0;
            if (bitdepth > 8) bitdepth = 8;
        }
    }

    /* Allocate software scale buffer when display size != render size */
    if (use_cyb && (scale_x != gfxvidinfo.width || scale_y != gfxvidinfo.height)) {
        bpp = GetCyberMapAttr (RP->BitMap, CYBRMATTR_BPPIX);
# ifndef USE_CYBERGFX_V41
        {
            int pixfmt = GetCyberMapAttr (RP->BitMap, CYBRMATTR_PIXFMT);

            /* CpuBuffer: CPU-RAM copy of the render bitmap for safe reading.
             * bufmem (DISPADR) may be hardware memory — not safe to read from
             * the CPU on all RTG drivers. We write to it normally, then also
             * keep a CPU copy for the scaling source. */
            CpuBufRowbytes = gfxvidinfo.width * bpp;
            CpuBuffer = AllocVec (CpuBufRowbytes * gfxvidinfo.height, MEMF_ANY | MEMF_CLEAR);

            /* ScaleBitMap: output bitmap at display size */
            ScaleBitMap = myAllocBitMap (scale_x, scale_y,
                                         RPDepth (RP),
                                         (pixfmt << 24) | BMF_SPECIALFMT | BMF_MINPLANES,
                                         RP->BitMap);

            if (!CpuBuffer || !ScaleBitMap) {
                write_log ("open_display: CpuBuffer/ScaleBitMap alloc failed, disabling scaling\n");
                if (CpuBuffer)  { FreeVec (CpuBuffer);  CpuBuffer  = NULL; CpuBufRowbytes = 0; }
                if (ScaleBitMap){ myFreeBitMap (ScaleBitMap); ScaleBitMap = NULL; }
                scale_x = gfxvidinfo.width;
                scale_y = gfxvidinfo.height;
            } else {
                /* ScaleBuffer matches ScaleBitMap stride exactly */
                ScaleBufRowbytes = (int) GetCyberMapAttr (ScaleBitMap, CYBRMATTR_XMOD);
                ScaleBuffer = AllocVec (ScaleBufRowbytes * scale_y, MEMF_ANY);
                if (!ScaleBuffer) {
                    write_log ("open_display: ScaleBuffer alloc failed, disabling scaling\n");
                    FreeVec (CpuBuffer);       CpuBuffer   = NULL; CpuBufRowbytes = 0;
                    myFreeBitMap (ScaleBitMap); ScaleBitMap = NULL;
                    scale_x = gfxvidinfo.width;
                    scale_y = gfxvidinfo.height;
                } else {
                    write_log ("open_display: scaling %dx%d->%dx%d CpuBuf stride=%d ScaleBuf stride=%d\n",
                               gfxvidinfo.width, gfxvidinfo.height,
                               scale_x, scale_y, CpuBufRowbytes, ScaleBufRowbytes);
                }
            }
        }
# else
        ScaleBufRowbytes = scale_x * bpp;
        ScaleBuffer = AllocVec (ScaleBufRowbytes * scale_y, MEMF_ANY);
        if (!ScaleBuffer) {
            write_log ("open_display: ScaleBuffer alloc failed, disabling scaling\n");
            scale_x = gfxvidinfo.width;
            scale_y = gfxvidinfo.height;
        } else {
            write_log ("open_display: ScaleBuffer %dx%d bpp=%d\n", scale_x, scale_y, bpp);
        }
# endif
    }
#endif

    if (is_ham) {
        use_delta_buffer       = 0;
        need_dither            = 0;
        gfxvidinfo.pixbytes    = 2;
        gfxvidinfo.flush_line  = flush_line_ham;
        gfxvidinfo.flush_block = flush_block_ham;
    } else if (bitdepth <= 8) {
        use_delta_buffer       = 1;
        need_dither            = currprefs.amiga_use_dither || (bitdepth <= 1);
        gfxvidinfo.pixbytes    = need_dither ? 2 : 1;
        gfxvidinfo.flush_line  = need_dither ? flush_line_planar_dither  : flush_line_planar_nodither;
        gfxvidinfo.flush_block = need_dither ? flush_block_planar_dither : flush_block_planar_nodither;
    }

#ifdef USE_CYBERGFX
    /* Override flush with scaled versions when display != render size */
# ifndef USE_CYBERGFX_V41
    if (use_cyb && CpuBuffer) {
        /* Redirect emulator rendering into CPU-RAM CpuBuffer.
         * flush_block_cgx_scaled reads from here (safe) rather than
         * from DISPADR (hardware memory, unsafe for CPU reads).
         * The non-scaled path uses DISPADR via CybBitMap as normal. */
        gfxvidinfo.bufmem   = (char *) CpuBuffer;
        gfxvidinfo.rowbytes = CpuBufRowbytes;
        gfxvidinfo.flush_line  = flush_line_cgx_scaled;
        gfxvidinfo.flush_block = flush_block_cgx_scaled;
    }
# else
    if (use_cyb && (scale_x != gfxvidinfo.width || scale_y != gfxvidinfo.height)) {
        gfxvidinfo.flush_line  = flush_line_cgx_scaled;
        gfxvidinfo.flush_block = flush_block_cgx_scaled;
    }
# endif
#endif

    gfxvidinfo.flush_clear_screen = flush_clear_screen_gfxlib;
    gfxvidinfo.flush_screen       = dummy_flush_screen;
    gfxvidinfo.lockscr            = dummy_lock;
    gfxvidinfo.unlockscr          = dummy_unlock;

    if (!use_cyb) {
        gfxvidinfo.rowbytes = gfxvidinfo.pixbytes * gfxvidinfo.width;
        gfxvidinfo.bufmem   = (uae_u8 *) calloc (gfxvidinfo.rowbytes, gfxvidinfo.height + 1);
    }
    if (!gfxvidinfo.bufmem) return 0;

    if (use_delta_buffer) {
        oldpixbuf = (uae_u8 *) calloc (gfxvidinfo.rowbytes, gfxvidinfo.height);
        if (!oldpixbuf) return 0;
    }

    if (!init_colors ()) return 0;

    /* Immediately reapply xcolors[] to all color tables so the palette
     * is correct from the very first frame — no refresh required. */
    notice_new_xcolors ();

    if (!usepub) ScreenToFront (S);

    /* Auto-grab mouse in fullscreen; release when windowed */
    if (!usepub) {
        mouseGrabbed = 1;
        grabTicks    = GRAB_TIMEOUT;
        if (W) grab_pointer (W);
    } else if (mouseGrabbed) {
        mouseGrabbed = 0;
        if (W) grab_pointer (W);
    }

    reset_drawing ();
    pointer_state = DONT_KNOW;
#ifdef PICASSO96
    /* a new window or screen: the card's whole picture again, or an idle
       RTG screen (nothing new drawn by the guest) stays black */
    p96_invalidate_all ();
#endif
    return 1;
}

void toggle_fullscreen (void)
{
    /* Defer to top of next handle_events — calling close_display from
     * inside the IntuiMessage loop (via hotkey) crashes on CloseWindow. */
    pending_toggle_fs = 1;
}

static void do_toggle_fullscreen (void)
{
    int new_type = is_fullscreen ()
        ? UAESCREENTYPE_PUBLIC
        : UAESCREENTYPE_ASK;

    write_log ("DISPLAY: toggle_fullscreen -> %s\n",
               new_type == UAESCREENTYPE_PUBLIC ? "windowed" : "fullscreen");

    close_display ();
    currprefs.amiga_screen_type     = new_type;
    changed_prefs.amiga_screen_type = new_type;

    if (!open_display ()) {
        write_log ("DISPLAY: toggle_fullscreen failed, reverting\n");
        currprefs.amiga_screen_type     = is_fullscreen ()
            ? UAESCREENTYPE_PUBLIC : UAESCREENTYPE_ASK;
        changed_prefs.amiga_screen_type = currprefs.amiga_screen_type;
        open_display ();
    }
}

void toggle_lores (void)
{
    pending_toggle_res = 1;
}

/* Called from ami-gui.c to open the ASL screen mode requester.
 * Deferred to top of handle_events so CloseWindow is never called
 * from inside an IntuiMessage handler.                             */
void toggle_screenmode (void)
{
    pending_screenmode  = 1;
    force_screenmode_req = 1;
}

static void do_toggle_lores (void)
{
    int old_lores = currprefs.gfx_lores;
    int old_w     = currprefs.gfx_width_win;
    int old_h     = currprefs.gfx_height_win;
    /* last_display_w/h persists across open_display() which zeroes display_w/h.
     * On first call it is 0 — old_w <= 320 branch handles that correctly.   */
    int old_dw    = last_display_w;
    int old_dh    = last_display_h;
    int new_lores, new_w, new_h, new_dw, new_dh;
    int fullscreen = is_fullscreen ();

    if (fullscreen) {
        /* Fullscreen: simple lores <-> hires toggle only.
         * get_displayid() picks the right screenmode based on gfx_lores. */
        if (old_lores) {
            new_lores = 0; new_w = 640; new_h = 512;
            write_log ("DISPLAY: toggle_lores (fs) -> hires 640x512\n");
        } else {
            new_lores = 1; new_w = 320; new_h = 256;
            write_log ("DISPLAY: toggle_lores (fs) -> lores 320x256\n");
        }
        new_dw = new_w;
        new_dh = new_h;
    } else {
        /* Windowed: 320x256 -> 640x512 -> 800x600 stretched -> 320x256 */
        if (old_w <= 320) {
            new_lores = 0; new_w = 640; new_h = 512;
            new_dw = 640; new_dh = 512;
            write_log ("DISPLAY: toggle_lores -> hires 640x512\n");
        } else if (old_dw < 800) {
            new_lores = 0; new_w = 640; new_h = 512;
            new_dw = 800; new_dh = 600;
            write_log ("DISPLAY: toggle_lores -> 800x600 stretched\n");
        } else {
            new_lores = 1; new_w = 320; new_h = 256;
            new_dw = 320; new_dh = 256;
            write_log ("DISPLAY: toggle_lores -> lores 320x256\n");
        }
    }

    currprefs.gfx_lores          = new_lores;
    changed_prefs.gfx_lores      = new_lores;
    currprefs.gfx_width_win      = new_w;
    currprefs.gfx_height_win     = new_h;
    currprefs.gfx_width_fs       = new_w;
    currprefs.gfx_height_fs      = new_h;
    changed_prefs.gfx_width_win  = new_w;
    changed_prefs.gfx_height_win = new_h;
    changed_prefs.gfx_width_fs   = new_w;
    changed_prefs.gfx_height_fs  = new_h;
    display_w      = new_dw;
    display_h      = new_dh;
    last_display_w = new_dw;
    last_display_h = new_dh;

    write_log ("DISPLAY: render %dx%d -> display %dx%d\n", new_w, new_h, new_dw, new_dh);

    close_display ();
    if (!open_display ()) {
        write_log ("DISPLAY: toggle_lores failed, reverting\n");
        currprefs.gfx_lores          = old_lores;
        changed_prefs.gfx_lores      = old_lores;
        currprefs.gfx_width_win      = old_w;
        currprefs.gfx_height_win     = old_h;
        currprefs.gfx_width_fs       = old_w;
        currprefs.gfx_height_fs      = old_h;
        changed_prefs.gfx_width_win  = old_w;
        changed_prefs.gfx_height_win = old_h;
        changed_prefs.gfx_width_fs   = old_w;
        changed_prefs.gfx_height_fs  = old_h;
        display_w      = old_dw;
        display_h      = old_dh;
        last_display_w = old_dw;
        last_display_h = old_dh;
        open_display ();
    }
}

void screenshot (int type)
{
    write_log ("Screenshot not implemented yet\n");
}

/****************************************************************************
 *
 * Mouse inputdevice functions
 */

#define MAX_BUTTONS     3
#define MAX_AXES        3
#define FIRST_AXIS      0
#define FIRST_BUTTON    MAX_AXES

static int init_mouse (void)
{
   return 1;
}

static void close_mouse (void)
{
   return;
}

static int acquire_mouse (unsigned int num, int flags)
{
   return 1;
}

static void unacquire_mouse (unsigned int num)
{
   return;
}

static unsigned int get_mouse_num (void)
{
    return 1;
}

static const char *get_mouse_name (unsigned int mouse)
{
    return "Default mouse";
}

static unsigned int get_mouse_widget_num (unsigned int mouse)
{
    return MAX_AXES + MAX_BUTTONS;
}

static int get_mouse_widget_first (unsigned int mouse, int type)
{
    switch (type) {
        case IDEV_WIDGET_BUTTON:
            return FIRST_BUTTON;
        case IDEV_WIDGET_AXIS:
            return FIRST_AXIS;
    }
    return -1;
}

static int get_mouse_widget_type (unsigned int mouse, unsigned int num, char *name, uae_u32 *code)
{
    if (num >= MAX_AXES && num < MAX_AXES + MAX_BUTTONS) {
        if (name)
            sprintf (name, "Button %d", num + 1 + MAX_AXES);
        return IDEV_WIDGET_BUTTON;
    } else if (num < MAX_AXES) {
        if (name)
            sprintf (name, "Axis %d", num + 1);
        return IDEV_WIDGET_AXIS;
    }
    return IDEV_WIDGET_NONE;
}

static void read_mouse (void)
{
    /* We handle mouse input in handle_events() */
}

struct inputdevice_functions inputdevicefunc_mouse = {
    init_mouse,
    close_mouse,
    acquire_mouse,
    unacquire_mouse,
    read_mouse,
    get_mouse_num,
    get_mouse_name,
    get_mouse_widget_num,
    get_mouse_widget_type,
    get_mouse_widget_first
};

/*
 * Default inputdevice config for mouse
 */
void input_get_default_mouse (struct uae_input_device *uid)
{
    /* Supports only one mouse for now */
    uid[0].eventid[ID_AXIS_OFFSET + 0][0]   = INPUTEVENT_MOUSE1_HORIZ;
    uid[0].eventid[ID_AXIS_OFFSET + 1][0]   = INPUTEVENT_MOUSE1_VERT;
    uid[0].eventid[ID_AXIS_OFFSET + 2][0]   = INPUTEVENT_MOUSE1_WHEEL;
    uid[0].eventid[ID_BUTTON_OFFSET + 0][0] = INPUTEVENT_JOY1_FIRE_BUTTON;
    uid[0].eventid[ID_BUTTON_OFFSET + 1][0] = INPUTEVENT_JOY1_2ND_BUTTON;
    uid[0].eventid[ID_BUTTON_OFFSET + 2][0] = INPUTEVENT_JOY1_3RD_BUTTON;
    uid[0].enabled = 1;
}

/****************************************************************************
 *
 * Keyboard inputdevice functions
 */
static unsigned int get_kb_num (void)
{
    return 1;
}

static const char *get_kb_name (unsigned int kb)
{
    return "Default keyboard";
}

static unsigned int get_kb_widget_num (unsigned int kb)
{
    return 128;
}

static int get_kb_widget_first (unsigned int kb, int type)
{
    return 0;
}

static int get_kb_widget_type (unsigned int kb, unsigned int num, char *name, uae_u32 *code)
{
    // fix me
    *code = num;
    return IDEV_WIDGET_KEY;
}

static int keyhack (int scancode, int pressed, int num)
{
    return scancode;
}

static void read_kb (void)
{
}

static int init_kb (void)
{
    return 1;
}

static void close_kb (void)
{
}

static int acquire_kb (unsigned int num, int flags)
{
    return 1;
}

static void unacquire_kb (unsigned int num)
{
}

struct inputdevice_functions inputdevicefunc_keyboard =
{
    init_kb,
    close_kb,
    acquire_kb,
    unacquire_kb,
    read_kb,
    get_kb_num,
    get_kb_name,
    get_kb_widget_num,
    get_kb_widget_type,
    get_kb_widget_first
};

int getcapslockstate (void)
{
    return 0;
}

void setcapslockstate (int state)
{
}

/****************************************************************************
 *
 * Handle gfx specific cfgfile options
 */

static const char *screen_type[] = { "custom", "public", "ask", 0 };

void gfx_default_options (struct uae_prefs *p)
{
    p->amiga_screen_type     = UAESCREENTYPE_PUBLIC;
    p->amiga_publicscreen[0] = '\0';
    p->amiga_use_dither      = 1;
    p->amiga_use_grey        = 0;
}

void gfx_save_options (FILE *f, const struct uae_prefs *p)
{
    cfgfile_write (f, GFX_NAME ".screen_type=%s\n",  screen_type[p->amiga_screen_type]);
    cfgfile_write (f, GFX_NAME ".publicscreen=%s\n", p->amiga_publicscreen);
    cfgfile_write (f, GFX_NAME ".use_dither=%s\n",   p->amiga_use_dither ? "true" : "false");
    cfgfile_write (f, GFX_NAME ".use_grey=%s\n",     p->amiga_use_grey ? "true" : "false");
}

int gfx_parse_option (struct uae_prefs *p, const char *option, const char *value)
{
    return (cfgfile_yesno  (option, value, "use_dither",   &p->amiga_use_dither)
	 || cfgfile_yesno  (option, value, "use_grey",	 &p->amiga_use_grey)
         || cfgfile_strval (option, value, "screen_type",  &p->amiga_screen_type, screen_type, 0)
         || cfgfile_string (option, value, "publicscreen", &p->amiga_publicscreen[0], 256)
    );
}

/****************************************************************************/

/* -------- Modal pause loop (AmigaOS) --------
 * Blocks emulation like a requester, resumes when the
 * FREEZEBUTTON hotkey (Ctrl+LAlt+P) is pressed again.
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>

extern struct Window *W; /* main window from this TU */

void ami_pause_loop(void)
{
    if (!W || !W->UserPort)
        return;

    for (;;) {

        /* --- Do pending quick-state work safely while paused --- */
        if (pending_quick_save) {
            pending_quick_save = 0;
            write_log("QUICKSTATE: performing SAVE in pause loop\n");
            if (gui_state_filename[0]) {
                savestate_initsave(gui_state_filename, 0);
                gui_state_filename[0] = '\0';
            }
            savestate_quick(1, 1);   /* slot 1, save */

            /* Prevent "stuck Ctrl+LAlt hotkey chord" after save */
            frf_block_hotkeys_until_release = 1;

            return;                  /* auto-unpause after saving */
        }

        if (pending_quick_load) {
            pending_quick_load = 0;
            write_log("QUICKSTATE: performing LOAD in pause loop\n");
            if (gui_state_filename[0]) {
                savestate_initsave(gui_state_filename, 0);
                gui_state_filename[0] = '\0';
            }
            savestate_quick(1, 0);   /* slot 1, load */

            /* Prevent "stuck Ctrl+LAlt hotkey chord" after load */
            frf_block_hotkeys_until_release = 1;

            /* stay paused after LOAD; user can unpause with freeze hotkey */
        }
        /* ------------------------------------------------------- */

        /* Yield CPU for one tick then poll for messages.
         * Delay(1) is safe under Executive; WaitPort is not used here. */
        Delay(1);

        /* Pump GUI window events while paused so the GUI remains interactive */
        gui_handle_events();

        struct IntuiMessage *im;
        while ((im = (struct IntuiMessage*)GetMsg(W->UserPort)) != NULL) {

            if (im->Class == IDCMP_RAWKEY) {

                UWORD code      = im->Code;
                UWORD qualifier = im->Qualifier;

                int   down = !(code & IECODE_UP_PREFIX);
                UWORD sc   = (code & ~IECODE_UP_PREFIX);

                /* Feed the matcher even while paused, so its internal state can unwind */
                int ev = match_hotkey_sequence((int)sc, down);

                ReplyMsg((struct Message*)im);

                /* If we are blocking hotkeys until chord release, clear when Ctrl/LAlt are up */
                if (frf_block_hotkeys_until_release) {
                    if ((qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT)) == 0)
                        frf_block_hotkeys_until_release = 0;
                }

                /* Ctrl+LAlt+B: toggle GUI window even while paused */
                if (down && sc == 0x35 &&
                    (qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT))
                        == (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT)) {
                    gui_toggle();
                }

                /* Unpause when the freeze hotkey is pressed again */
                if (down && ev == INPUTEVENT_SPC_FREEZEBUTTON)
                    return;

                /* Allow queuing quick actions while paused, too */
                if (down && ev == INPUTEVENT_SPC_STATESAVEQUICK) {
                    pending_quick_save = 1;
                } else if (down && ev == INPUTEVENT_SPC_STATERESTOREQUICK) {
                    pending_quick_load = 1;
                }

            } else {
                /* Auto-resume when focus returns after a focus-loss pause */
                if (ami_pause_focus_loss && im->Class == IDCMP_ACTIVEWINDOW) {
                    ReplyMsg((struct Message*)im);
                    /* Re-acquire input and clear stuck keys, same as normal focus-gain */
                    inputdevice_acquire ();
                    inputdevice_release_all_keys ();
                    reset_hotkeys ();
                    ami_pause_focus_loss = 0;
                    return;
                }
                ReplyMsg((struct Message*)im);
            }
        }
    }
}


