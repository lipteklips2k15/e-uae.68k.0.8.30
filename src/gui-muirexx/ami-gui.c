/* Future Retro Fusion */
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * GUI interface (to be done).
  * Calls AREXX interface.
  *
  * Copyright 1996 Bernd Schmidt, Samuel Devulder
  * Copyright 2004-2006 Richard Drummond
  */

#include "sysconfig.h"

#include "sysdeps.h"

#include "options.h"
#include "gui.h"
#include "disk.h"
#include "savestate.h"
#include "uae.h"

#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <dos/dosextens.h>

/****************************************************************************/

extern int  rexx_init (void);
extern void rexx_exit (void);
extern void rexx_led (int led, int on);          /* ami-rexx.c */
extern void rexx_filename (int num, const char *name);
extern void rexx_handle_events (void);
extern void main_window_led (int led, int on);   /* ami-win.c */

/****************************************************************************/

extern struct AslIFace *IAsl;

/* File dialog types */
#define FILEDIALOG_INSERT_DF0    0
#define FILEDIALOG_INSERT_DF1    1
#define FILEDIALOG_INSERT_DF2    2
#define FILEDIALOG_INSERT_DF3    3
#define FILEDIALOG_LOAD_STATE    4
#define FILEDIALOG_SAVE_STATE    5
#define FILEDIALOG_MAX           6

#define FILEDIALOG_DRIVE(x) ((x)-FILEDIALOG_INSERT_DF0)

/* For remembering last directory used in file requesters */

static char *last_floppy_dir;
static char *last_savestate_dir;

static void free_last_floppy_dir (void)
{
    if (last_floppy_dir) {
	free (last_floppy_dir);
	last_floppy_dir = 0;
    }
}

static void free_last_savestate_dir (void)
{
    if (last_savestate_dir) {
	free (last_savestate_dir);
	last_savestate_dir = 0;
    }
}

static const char *get_last_floppy_dir (void)
{
    if (!last_floppy_dir) {
	static int done = 0;
	unsigned int len;

	if (!done) {
	    done = 1;
	    atexit (free_last_floppy_dir);
	}

        last_floppy_dir = my_strdup (prefs_get_attr ("floppy_path"));
    }
    return last_floppy_dir;
}

static const char *get_last_savestate_dir (void)
{
    if (!last_savestate_dir) {
	static int done = 0;
	unsigned int len;

	if (!done) {
	    done = 1;
	    atexit (free_last_savestate_dir);
	}

        last_savestate_dir = my_strdup (prefs_get_attr ("savestate_path"));
    }
    return last_savestate_dir;
}

static void set_last_floppy_dir (const char *path)
{
    if (last_floppy_dir) {
	free (last_floppy_dir);
	last_floppy_dir = 0;
    }

    if (path) {
	unsigned int len = strlen (path);
	if (len) {
	    last_floppy_dir = malloc (len + 1);
	    if (last_floppy_dir)
		strcpy (last_floppy_dir, path);
	}
    }
}

static void set_last_savestate_dir (const char *path)
{
    if (last_savestate_dir) {
	free (last_savestate_dir);
	last_savestate_dir = 0;
    }

    if (path) {
	unsigned int len = strlen (path);
	if (len) {
	    last_savestate_dir = malloc (len + 1);
	    if (last_savestate_dir)
		strcpy (last_savestate_dir, path);
	}
    }
}

static void do_file_dialog (unsigned int type)
{
    struct FileRequester *FileRequest;
    struct Window *win;

    char buf[80];
    char path[512];

    const char *req_prompt;
    const char *req_pattern = 0;
    const char *req_lastdir;
    int         req_do_save = FALSE;

#ifdef __amigaos4__
    int release_asl = 0;
#endif

    if (type >= FILEDIALOG_MAX)
	return;

    if (!AslBase) {
	AslBase = OpenLibrary ("asl.library", 36);
	if (!AslBase) {
	    write_log ("Can't open asl.library v36.\n");
	    return;
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

    FileRequest = AllocAslRequest (ASL_FileRequest, NULL);
    if (!FileRequest) {
	write_log ("Unable to allocate file requester.\n");
	return;
    }

    /* Find this task's default window */
    win = ((struct Process *) FindTask (NULL))->pr_WindowPtr;
    if (win == (struct Window *)-1)
	win = 0;

    /*
     * Prepare requester.
     */
    switch (type) {

	default: /* to stop GCC complaining */
	case FILEDIALOG_INSERT_DF0:
	case FILEDIALOG_INSERT_DF1:
	case FILEDIALOG_INSERT_DF2:
	case FILEDIALOG_INSERT_DF3:
	    sprintf (buf, "Select image to insert in drive DF%d:", FILEDIALOG_DRIVE(type));
	    req_prompt = buf;
	    req_pattern = "(#?.(ad(f|z)|dms|ipf|zip)#?|df?|?)";
	    req_lastdir = get_last_floppy_dir ();
	    break;

	case FILEDIALOG_SAVE_STATE:
	    req_prompt = "Select file to save emulator state to\n";
	    req_pattern = "#?.uss";
	    req_lastdir = get_last_savestate_dir ();
	    req_do_save = TRUE;
	    break;

	case FILEDIALOG_LOAD_STATE:
	    req_prompt = "Select saved state file to load";
	    req_pattern = "#?.uss";
	    req_lastdir = get_last_savestate_dir ();
	    break;
    }

    /*
     * Do the file request.
     */
    if (AslRequestTags (FileRequest,
			ASLFR_TitleText,      req_prompt,
			ASLFR_InitialDrawer,  req_lastdir,
			ASLFR_InitialPattern, req_pattern,
			ASLFR_DoPatterns,     req_pattern != 0,
			ASLFR_DoSaveMode,     req_do_save,
			ASLFR_RejectIcons,    TRUE,
			ASLFR_Window,         win,
			TAG_DONE)) {

	/*
	 * User selected a file.
	 *
	 * Construct file path to selected image.
	 */
	strcpy (path, FileRequest->fr_Drawer);
	if (strlen (path) && !(path[strlen (path) - 1] == ':' || path[strlen (path) - 1] == '/'))
	    strcat (path, "/");
	strcat (path, FileRequest->fr_File);

        /*
	 * Process selected file.
	 */
	switch (type) {

	    default: /* to stop GCC complaining */
	    case FILEDIALOG_INSERT_DF0:
	    case FILEDIALOG_INSERT_DF1:
	    case FILEDIALOG_INSERT_DF2:
	    case FILEDIALOG_INSERT_DF3:
		set_last_savestate_dir ((const char *)FileRequest->fr_Drawer);
		strcpy (changed_prefs.df[FILEDIALOG_DRIVE(type)], path);
		break;

	    case FILEDIALOG_SAVE_STATE:
		set_last_savestate_dir ((const char *)FileRequest->fr_Drawer);
		savestate_initsave (path, 1);
		save_state (path, "Description");
		break;

	    case FILEDIALOG_LOAD_STATE:
		set_last_savestate_dir ((const char *)FileRequest->fr_Drawer);
		savestate_initsave (path, 1);
		savestate_state = STATE_DORESTORE;
		write_log ("Restoring state from '%s'...\n", path);
		break;
	}
    }

    FreeAslRequest (FileRequest);

#ifdef __amigaos4__
    if (release_asl)
        IAsl->Release ();
#endif

    return;
}

/****************************************************************************/

void gui_init (int argc, char **argv)
{
}

/****************************************************************************/

static int have_rexx = 0;

int gui_open (void)
{
    if (!have_rexx) {
	have_rexx = rexx_init ();

	if (have_rexx)
	   atexit (rexx_exit);
    }
    return -1;
}

/****************************************************************************/

extern struct Window *ami_main_window (void);   /* ami-win.c */
static void gui_close (void);
static void gui_open_window (void);
static void gui_events (void);
static struct Window *gui_win;
static int gui_pending_toggle;
static struct Gadget *df_text[4];
static const char *file_part (const char *path);

void gui_exit (void)
{
    if (gui_win)
        gui_close ();
}

/****************************************************************************/

int gui_update (void)
{
    return 0;
}

/****************************************************************************/

void gui_led (int led, int on)
{
    if (have_rexx)
	rexx_led (led, on);
}

/****************************************************************************/

void gui_filename (int num, const char *name)
{
    if (have_rexx)
	rexx_filename (num, name);
    if (gui_win && num >= 0 && num < 4 && df_text[num])
	GT_SetGadgetAttrs (df_text[num], gui_win, NULL,
			   GTTX_Text, name && name[0] ? file_part (name) : "(empty)", TAG_DONE);
}

/****************************************************************************/

void gui_handle_events (void)
{
    if (have_rexx)
	rexx_handle_events();

    if (gui_pending_toggle) {
	gui_pending_toggle = 0;
	if (gui_win)
	    gui_close ();
	else if (ami_main_window ())
	    gui_open_window ();
    }
    if (gui_win)
	gui_events ();
}

/****************************************************************************/

void gui_notify_state (int state)
{
}

/****************************************************************************/

void gui_hd_led (int led)
{
    static int resetcounter;

    int old = gui_data.hd;

    if (led == 0) {
	resetcounter--;
	if (resetcounter > 0)
	    return;
    }

    gui_data.hd = led;
    resetcounter = 6;
    if (old != gui_data.hd)
	gui_led (5, gui_data.hd);
}

/****************************************************************************/

void gui_cd_led (int led)
{
    static int resetcounter;

    int old = gui_data.cd;
    if (led == 0) {
	resetcounter--;
	if (resetcounter > 0)
	    return;
    }

    gui_data.cd = led;
    resetcounter = 6;
    if (old != gui_data.cd)
	gui_led (6, gui_data.cd);
}

/****************************************************************************/

void gui_fps (int fps, int idle)
{
    gui_data.fps  = fps;
    gui_data.idle = idle;
}

/****************************************************************************/

/*
 * Native GUI: a GadTools window, opened and closed with Ctrl+LAlt+B (or
 * closed with Esc / its close gadget). It goes on the emulator's public
 * screen when E-UAE runs in a window there, otherwise on a screen of its
 * own like Workbench's, so a fullscreen emulator palette can't spoil it.
 * The emulator keeps running while it is open.
 *
 * gui_toggle() is called from inside ami-win.c's IntuiMessage loops, so it
 * only sets a flag; gui_handle_events() opens or closes the window.
 */

#define GUI_W   468             /* inner size */
#define GUI_H   166
#define GH      14              /* gadget height, topaz 8 */
#define ROW     19

enum { PAGE_DRIVES, PAGE_STATE, PAGE_EMULATOR, PAGE_SETTINGS, PAGE_COUNT };
static const char *page_names[PAGE_COUNT] = { "Drives", "State", "Emulator", "Settings" };

enum {
    GID_PAGE = 1,                               /* + page */
    GID_DF_INSERT = 10,                         /* + drive */
    GID_DF_EJECT = 20,                          /* + drive */
    GID_LOADSTATE = 30, GID_SAVESTATE, GID_SOFTRESET, GID_HARDRESET,
    GID_STATELIST, GID_STATELOAD,
    GID_SOUND = 40, GID_VOLUME, GID_FRAMERATE, GID_LEDS,
    GID_LORES, GID_FULLSCREEN, GID_SCREENMODE,
    GID_CPU = 50, GID_COMPAT, GID_ROM, GID_LOADCFG, GID_SAVECFG
};

extern struct Window *ami_main_window (void);  /* ami-win.c */
extern int  ami_main_on_pubscreen (void);
extern void toggle_lores (void);
extern void toggle_fullscreen (void);
extern void toggle_screenmode (void);
extern void frf_volume_set_percent (int percent);
extern void notice_screen_contents_lost (void);

struct Library *GadToolsBase;

int gui_screen_open = 0;                /* ami-win.c: no focus-loss pause */

static int             gui_pending_toggle;
static struct Screen  *gui_scr;
static int             gui_own_scr;     /* gui_scr is ours to close */
static struct Window  *gui_win;
static APTR            gui_vi;
static struct Gadget  *gui_glist;
static struct Gadget  *df_text[4];
static struct Gadget  *rom_text;
static int             gui_page;
static int             gui_x0, gui_y0;  /* the window's inner origin */
static struct TextAttr gui_font = { (STRPTR) "topaz.font", 8, 0, 0 };
static char            gui_title[96];

static const char *sound_labels[] = { "Off", "Normal", "Best", 0 };
static const char *cpu_labels[] = { "68000", "68010", "68020", "68020/68881", "68040", "68060", 0 };
static const int   cpu_levels[] = { 0, 1, 2, 3, 4, 6 };

/* The saved-state list: S:uae-states.csv, one "label,path" per line,
 * appended by every save from the GUI; shown newest first. */
#define STATES_CSV  "S:uae-states.csv"
#define MAX_STATES  32
static struct List state_list;
static struct Node state_nodes[MAX_STATES];
static char        state_label[MAX_STATES][64];
static char        state_path[MAX_STATES][256];
static int         state_count;
static int         state_sel = -1;

static const char *file_part (const char *path)
{
    const char *p = path, *s;
    for (s = path; *s; s++)
        if (*s == '/' || *s == ':')
            p = s + 1;
    return p;
}

static void states_read (void)
{
    FILE *f;
    char line[400];
    int i;

    state_list.lh_Head     = (struct Node *) &state_list.lh_Tail;
    state_list.lh_Tail     = 0;
    state_list.lh_TailPred = (struct Node *) &state_list.lh_Head;
    state_count = 0;
    state_sel = -1;

    if ((f = fopen (STATES_CSV, "r"))) {
        while (fgets (line, sizeof line, f)) {
            char *p = line + strlen (line), *comma;
            const char *label = line, *path = line;

            while (p > line && (p[-1] == '\n' || p[-1] == '\r'))
                *--p = 0;
            if (!line[0])
                continue;
            if ((comma = strchr (line, ','))) {
                *comma = 0;
                path = comma + 1;
            }
            if (state_count == MAX_STATES) {    /* keep the newest */
                memmove (state_label[0], state_label[1], sizeof state_label[0] * (MAX_STATES - 1));
                memmove (state_path[0], state_path[1], sizeof state_path[0] * (MAX_STATES - 1));
                state_count--;
            }
            strncpy (state_label[state_count], label, sizeof state_label[0] - 1);
            state_label[state_count][sizeof state_label[0] - 1] = 0;
            strncpy (state_path[state_count], path, sizeof state_path[0] - 1);
            state_path[state_count][sizeof state_path[0] - 1] = 0;
            state_count++;
        }
        fclose (f);
    }
    for (i = 0; i < state_count; i++) {
        struct Node *n = &state_nodes[i];
        memset (n, 0, sizeof *n);
        n->ln_Name = state_label[state_count - 1 - i];
        AddTail (&state_list, n);
    }
}

static void states_add (const char *path)
{
    FILE *f = fopen (STATES_CSV, "a");
    if (f) {
        fprintf (f, "%s,%s\n", file_part (path), path);
        fclose (f);
    }
}

static void gui_status (const char *msg)
{
    if (!gui_win)
        return;
    snprintf (gui_title, sizeof gui_title, "E-UAE - %s", msg);
    SetWindowTitles (gui_win, (UBYTE *) gui_title, (UBYTE *) ~0);
    write_log ("GUI: %s\n", msg);
}

/* An ASL file requester on the GUI's screen; the full path goes to path. */
static int gui_asl (const char *title, const char *pattern, const char *drawer,
                    int save, char *path, int pathlen)
{
    struct FileRequester *fr;
    int ok = 0;

    if (!AslBase && !(AslBase = OpenLibrary ("asl.library", 36))) {
        write_log ("Can't open asl.library v36.\n");
        return 0;
    }
    if (!(fr = AllocAslRequest (ASL_FileRequest, NULL)))
        return 0;

    if (AslRequestTags (fr,
                        ASLFR_TitleText,      title,
                        ASLFR_Window,         gui_win,
                        ASLFR_SleepWindow,    TRUE,
                        ASLFR_InitialDrawer,  drawer ? drawer : "",
                        ASLFR_InitialPattern, pattern ? pattern : "#?",
                        ASLFR_DoPatterns,     pattern != 0,
                        ASLFR_DoSaveMode,     save,
                        ASLFR_RejectIcons,    TRUE,
                        TAG_DONE) && fr->fr_File[0]) {
        int len;
        strncpy (path, fr->fr_Drawer, pathlen - 1);
        path[pathlen - 1] = 0;
        len = strlen (path);
        if (len && path[len - 1] != ':' && path[len - 1] != '/' && len < pathlen - 1)
            strcat (path, "/");
        strncat (path, fr->fr_File, pathlen - 1 - strlen (path));
        ok = 1;
        /* remember the drawer for next time */
        if (title && strstr (title, "drive"))
            set_last_floppy_dir (fr->fr_Drawer);
        else if (title && strstr (title, "state"))
            set_last_savestate_dir (fr->fr_Drawer);
    }
    FreeAslRequest (fr);
    return ok;
}

/* ---- pages ---- */

/* noinline: gcc 6 -Ofast inlines this varargs helper and drops the tag values */
static __attribute__((noinline)) struct Gadget *mk (struct Gadget *prev, ULONG kind, int id, const char *label,
                          int x, int y, int w, int h, ULONG flags, Tag tag1, ...)
{
    struct NewGadget ng;
    struct TagItem tags[12];
    va_list ap;
    int n = 0;

    if (!prev)
        return 0;
    /* copy the tags out properly: &tag1 isn't the argument list under -Ofast */
    va_start (ap, tag1);
    for (tags[0].ti_Tag = tag1; tags[n].ti_Tag != TAG_DONE && n < 11; tags[n].ti_Tag = va_arg (ap, Tag)) {
        tags[n].ti_Data = va_arg (ap, ULONG);
        n++;
    }
    va_end (ap);
    tags[n].ti_Tag = TAG_DONE;
    ng.ng_LeftEdge   = gui_x0 + x;
    ng.ng_TopEdge    = gui_y0 + y;
    ng.ng_Width      = w;
    ng.ng_Height     = h;
    ng.ng_GadgetText = (UBYTE *) label;
    ng.ng_TextAttr   = &gui_font;
    ng.ng_GadgetID   = id;
    ng.ng_Flags      = flags;
    ng.ng_VisualInfo = gui_vi;
    ng.ng_UserData   = 0;
    return CreateGadgetA (kind, prev, &ng, tags);
}

static int cpu_index (void)
{
    int i;
    for (i = 5; i > 0; i--)
        if (changed_prefs.cpu_level >= cpu_levels[i])
            return i;
    return 0;
}

static struct Gadget *build_page (struct Gadget *g)
{
    int i, y = 24;

    df_text[0] = df_text[1] = df_text[2] = df_text[3] = 0;
    rom_text = 0;

    for (i = 0; i < PAGE_COUNT; i++)
        g = mk (g, BUTTON_KIND, GID_PAGE + i, page_names[i], 4 + i * 116, 2, 112, GH, 0,
                GA_Disabled, i == gui_page, TAG_DONE);

    switch (gui_page) {

    case PAGE_DRIVES:
        for (i = 0; i < 4; i++, y += ROW) {
            static const char *dfn[4] = { "DF0:", "DF1:", "DF2:", "DF3:" };
            const char *name = currprefs.df[i][0] ? file_part (currprefs.df[i]) : "(empty)";
            g = df_text[i] = mk (g, TEXT_KIND, 0, dfn[i], 44, y, 264, GH, PLACETEXT_LEFT,
                                 GTTX_Text, name, GTTX_CopyText, TRUE, GTTX_Border, TRUE, TAG_DONE);
            g = mk (g, BUTTON_KIND, GID_DF_INSERT + i, "Insert...", 314, y, 76, GH, 0, TAG_DONE);
            g = mk (g, BUTTON_KIND, GID_DF_EJECT + i, "Eject", 394, y, 68, GH, 0,
                    GA_Disabled, !currprefs.df[i][0], TAG_DONE);
        }
        break;

    case PAGE_STATE:
        g = mk (g, BUTTON_KIND, GID_LOADSTATE, "Load state...", 4, y, 112, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_SAVESTATE, "Save state...", 120, y, 112, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_SOFTRESET, "Soft reset", 236, y, 112, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_HARDRESET, "Hard reset", 352, y, 112, GH, 0, TAG_DONE);
        y += ROW + 12;
        states_read ();
        g = mk (g, LISTVIEW_KIND, GID_STATELIST, "Saved states", 4, y, 460, 84, PLACETEXT_ABOVE,
                GTLV_Labels, &state_list, GTLV_ShowSelected, 0, TAG_DONE);
        y += 88;
        g = mk (g, BUTTON_KIND, GID_STATELOAD, "Load selected", 4, y, 112, GH, 0,
                GA_Disabled, TRUE, TAG_DONE);
        break;

    case PAGE_EMULATOR: {
        int snd = changed_prefs.produce_sound <= 1 ? 0 : changed_prefs.produce_sound == 2 ? 1 : 2;
        int fr = currprefs.gfx_framerate < 1 ? 1 : currprefs.gfx_framerate > 10 ? 10 : currprefs.gfx_framerate;
        g = mk (g, CYCLE_KIND, GID_SOUND, "Sound", 100, y, 140, GH, PLACETEXT_LEFT,
                GTCY_Labels, sound_labels, GTCY_Active, snd, TAG_DONE);
        y += ROW;
        g = mk (g, SLIDER_KIND, GID_VOLUME, "Volume", 100, y, 260, GH - 2, PLACETEXT_LEFT,
                GTSL_Min, 0, GTSL_Max, 100, GTSL_Level, currprefs.sound_volume,
                GTSL_LevelFormat, "%3ld%%", GTSL_MaxLevelLen, 4, GTSL_LevelPlace, PLACETEXT_RIGHT,
                GA_RelVerify, TRUE, TAG_DONE);
        y += ROW;
        g = mk (g, SLIDER_KIND, GID_FRAMERATE, "Draw every", 100, y, 260, GH - 2, PLACETEXT_LEFT,
                GTSL_Min, 1, GTSL_Max, 10, GTSL_Level, fr,
                GTSL_LevelFormat, "%2ld. frame", GTSL_MaxLevelLen, 9, GTSL_LevelPlace, PLACETEXT_RIGHT,
                GA_RelVerify, TRUE, TAG_DONE);
        y += ROW;
        g = mk (g, CHECKBOX_KIND, GID_LEDS, "On-screen LEDs", 100, y, 26, 11, PLACETEXT_LEFT,
                GTCB_Checked, currprefs.leds_on_screen, TAG_DONE);
        y += ROW + 6;
        g = mk (g, BUTTON_KIND, GID_LORES, "Lores / size", 4, y, 148, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_FULLSCREEN, "Window / full", 160, y, 148, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_SCREENMODE, "Screen mode...", 316, y, 148, GH, 0, TAG_DONE);
        break;
    }

    case PAGE_SETTINGS:
        g = mk (g, CYCLE_KIND, GID_CPU, "CPU", 100, y, 140, GH, PLACETEXT_LEFT,
                GTCY_Labels, cpu_labels, GTCY_Active, cpu_index (), TAG_DONE);
        y += ROW;
        g = mk (g, CHECKBOX_KIND, GID_COMPAT, "Compatible", 100, y, 26, 11, PLACETEXT_LEFT,
                GTCB_Checked, changed_prefs.cpu_compatible, TAG_DONE);
        y += ROW;
        g = rom_text = mk (g, TEXT_KIND, 0, "Kickstart", 100, y, 280, GH, PLACETEXT_LEFT,
                           GTTX_Text, file_part (changed_prefs.romfile), GTTX_CopyText, TRUE,
                           GTTX_Border, TRUE, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_ROM, "ROM...", 386, y, 78, GH, 0, TAG_DONE);
        y += ROW + 6;
        g = mk (g, BUTTON_KIND, GID_LOADCFG, "Load config...", 4, y, 148, GH, 0, TAG_DONE);
        g = mk (g, BUTTON_KIND, GID_SAVECFG, "Save config...", 160, y, 148, GH, 0, TAG_DONE);
        y += ROW + 4;
        g = mk (g, TEXT_KIND, 0, 0, 4, y, 460, GH, 0,
                GTTX_Text, "CPU now; ROM and loaded configs at the next hard reset", TAG_DONE);
        break;
    }
    return g;
}

static void show_page (int page)
{
    struct Gadget *g;

    if (gui_glist) {
        RemoveGList (gui_win, gui_glist, -1);
        FreeGadgets (gui_glist);
        gui_glist = 0;
    }
    gui_page = page;
    SetAPen (gui_win->RPort, 0);
    RectFill (gui_win->RPort, gui_x0, gui_y0, gui_x0 + GUI_W - 1, gui_y0 + GUI_H - 1);

    g = CreateContext (&gui_glist);
    if (!build_page (g)) {
        write_log ("GUI: gadget creation failed\n");
        FreeGadgets (gui_glist);
        gui_glist = 0;
        return;
    }
    AddGList (gui_win, gui_glist, (UWORD) ~0, -1, NULL);
    RefreshGList (gui_glist, gui_win, NULL, -1);
    GT_RefreshWindow (gui_win, NULL);
}

/* ---- open / close ---- */

static void gui_close (void)
{
    struct Window *mw = ami_main_window ();

    if (gui_win) {
        if (gui_glist)
            RemoveGList (gui_win, gui_glist, -1);
        CloseWindow (gui_win);
        gui_win = 0;
    }
    if (gui_glist) {
        FreeGadgets (gui_glist);
        gui_glist = 0;
    }
    if (gui_vi) {
        FreeVisualInfo (gui_vi);
        gui_vi = 0;
    }
    if (gui_scr) {
        if (gui_own_scr)
            CloseScreen (gui_scr);
        else
            UnlockPubScreen (NULL, gui_scr);
        gui_scr = 0;
    }
    if (mw) {
        ScreenToFront (mw->WScreen);
        ActivateWindow (mw);
    }
    gui_screen_open = 0;
    write_log ("GUI: closed\n");
}

static void gui_open_window (void)
{
    struct Window *mw = ami_main_window ();
    static UWORD pens[] = { (UWORD) ~0 };
    int left, top;

    if (!GadToolsBase && !(GadToolsBase = OpenLibrary ("gadtools.library", 37))) {
        write_log ("GUI: can't open gadtools.library v37\n");
        return;
    }

    gui_own_scr = 0;
    gui_scr = 0;
    /* A window on the emulator's public screen, if it runs in one. */
    if (ami_main_on_pubscreen () && mw->WScreen) {
        struct Screen *wb = LockPubScreen (NULL);
        if (wb == mw->WScreen)
            gui_scr = wb;
        else if (wb)
            UnlockPubScreen (NULL, wb);
    }
    /* Otherwise a screen of our own like Workbench's (hires as fallback). */
    if (!gui_scr) {
        gui_scr = OpenScreenTags (NULL,
                                  SA_LikeWorkbench, TRUE,
                                  SA_Title,  (ULONG) "E-UAE",
                                  SA_Pens,   (ULONG) pens,
                                  SA_Font,   (ULONG) &gui_font,
                                  TAG_DONE);
        if (!gui_scr)
            gui_scr = OpenScreenTags (NULL,
                                      SA_Width, 640, SA_Height, 256, SA_Depth, 2,
                                      SA_DisplayID, HIRES_KEY,
                                      SA_Title, (ULONG) "E-UAE",
                                      SA_Pens,  (ULONG) pens,
                                      SA_Font,  (ULONG) &gui_font,
                                      TAG_DONE);
        if (!gui_scr) {
            write_log ("GUI: can't open a screen\n");
            return;
        }
        gui_own_scr = 1;
    }

    if (!(gui_vi = GetVisualInfo (gui_scr, TAG_DONE))) {
        write_log ("GUI: no VisualInfo\n");
        gui_close ();
        return;
    }

    left = (gui_scr->Width - GUI_W) / 2;
    top  = (gui_scr->Height - GUI_H) / 2;
    if (left < 0) left = 0;
    if (top < 0)  top = 0;

    gui_screen_open = 1;                /* before the emulator loses focus */
    gui_win = OpenWindowTags (NULL,
                              WA_CustomScreen, (ULONG) gui_scr,
                              WA_Left,         left,
                              WA_Top,          top,
                              WA_InnerWidth,   GUI_W,
                              WA_InnerHeight,  GUI_H,
                              WA_Title,        (ULONG) "E-UAE  (Ctrl+LAlt+B or Esc closes)",
                              WA_DragBar,      TRUE,
                              WA_DepthGadget,  TRUE,
                              WA_CloseGadget,  TRUE,
                              WA_Activate,     TRUE,
                              WA_SimpleRefresh, TRUE,
                              WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY
                                               | BUTTONIDCMP | CYCLEIDCMP | CHECKBOXIDCMP
                                               | SLIDERIDCMP | LISTVIEWIDCMP,
                              TAG_DONE);
    if (!gui_win) {
        write_log ("GUI: can't open the window\n");
        gui_close ();
        return;
    }
    gui_x0 = gui_win->BorderLeft;
    gui_y0 = gui_win->BorderTop;
    if (gui_own_scr)
        ScreenToFront (gui_scr);
    show_page (gui_page);
    write_log ("GUI: open on %s screen\n", gui_own_scr ? "its own" : "the emulator's");
}

void gui_toggle (void)
{
    gui_pending_toggle = 1;
}

/* ---- events ---- */

static void gui_gadget (int id, int code)
{
    char path[512];

    if (id >= GID_PAGE && id < GID_PAGE + PAGE_COUNT) {
        show_page (id - GID_PAGE);
        return;
    }
    if (id >= GID_DF_INSERT && id < GID_DF_INSERT + 4) {
        int n = id - GID_DF_INSERT;
        char title[48];
        sprintf (title, "Image to insert in drive DF%d:", n);
        if (gui_asl (title, "(#?.(ad(f|z)|dms|ipf|zip)#?|df?|?)", get_last_floppy_dir (), 0, path, sizeof path)) {
            strncpy (changed_prefs.df[n], path, sizeof changed_prefs.df[n] - 1);
            changed_prefs.df[n][sizeof changed_prefs.df[n] - 1] = 0;
            disk_insert (n, changed_prefs.df[n]);
            if (df_text[n])
                GT_SetGadgetAttrs (df_text[n], gui_win, NULL, GTTX_Text, file_part (path), TAG_DONE);
            show_page (PAGE_DRIVES);
            gui_status ("disk inserted");
        }
        return;
    }
    if (id >= GID_DF_EJECT && id < GID_DF_EJECT + 4) {
        disk_eject (id - GID_DF_EJECT);
        show_page (PAGE_DRIVES);
        gui_status ("disk ejected");
        return;
    }

    switch (id) {
    case GID_LOADSTATE:
    case GID_SAVESTATE: {
        int save = id == GID_SAVESTATE;
        if (!gui_asl (save ? "Save emulator state to" : "Saved state to load", "#?.uss",
                      get_last_savestate_dir (), save, path, sizeof path))
            break;
        if (save) {
            if (strlen (path) < 4 || strcmp (path + strlen (path) - 4, ".uss"))
                strncat (path, ".uss", sizeof path - 1 - strlen (path));
            savestate_initsave (path, 0);
            save_state (path, "");
            states_add (path);
            show_page (PAGE_STATE);
            gui_status ("state saved");
        } else {
            savestate_initsave (path, 0);
            strncpy (savestate_fname, path, sizeof savestate_fname - 1);
            savestate_fname[sizeof savestate_fname - 1] = 0;
            savestate_state = STATE_DORESTORE;
            gui_status ("restoring state");
        }
        break;
    }
    case GID_STATELIST:
        state_sel = code;
        if (gui_glist) {
            struct Gadget *g;
            for (g = gui_glist; g; g = g->NextGadget)
                if (g->GadgetID == GID_STATELOAD)
                    GT_SetGadgetAttrs (g, gui_win, NULL, GA_Disabled, FALSE, TAG_DONE);
        }
        break;
    case GID_STATELOAD:
        if (state_sel >= 0 && state_sel < state_count) {
            const char *p = state_path[state_count - 1 - state_sel];
            savestate_initsave (p, 0);
            strncpy (savestate_fname, p, sizeof savestate_fname - 1);
            savestate_fname[sizeof savestate_fname - 1] = 0;
            savestate_state = STATE_DORESTORE;
            gui_status ("restoring state");
        }
        break;
    case GID_SOFTRESET:
        uae_reset (0);
        gui_status ("soft reset");
        break;
    case GID_HARDRESET:
        uae_reset (1);
        gui_status ("hard reset");
        break;

    case GID_SOUND: {
        static const int snd[3] = { 0, 2, 3 };
        changed_prefs.produce_sound = snd[code < 0 ? 0 : code > 2 ? 2 : code];
        break;
    }
    case GID_VOLUME:
        frf_volume_set_percent (code);
        break;
    case GID_FRAMERATE:
        currprefs.gfx_framerate = changed_prefs.gfx_framerate = code;
        break;
    case GID_LEDS:
        currprefs.leds_on_screen = changed_prefs.leds_on_screen = !currprefs.leds_on_screen;
        notice_screen_contents_lost ();
        break;
    case GID_LORES:
        toggle_lores ();
        break;
    case GID_FULLSCREEN:
        toggle_fullscreen ();
        gui_pending_toggle = 1;         /* the display is rebuilt: close */
        break;
    case GID_SCREENMODE:
        toggle_screenmode ();
        gui_pending_toggle = 1;
        break;

    case GID_CPU:
        changed_prefs.cpu_level = cpu_levels[code < 0 ? 0 : code > 5 ? 5 : code];
        break;
    case GID_COMPAT:
        changed_prefs.cpu_compatible = !changed_prefs.cpu_compatible;
        break;
    case GID_ROM:
        if (gui_asl ("Kickstart ROM image", "#?", 0, 0, path, sizeof path)) {
            strncpy (changed_prefs.romfile, path, sizeof changed_prefs.romfile - 1);
            changed_prefs.romfile[sizeof changed_prefs.romfile - 1] = 0;
            if (rom_text)
                GT_SetGadgetAttrs (rom_text, gui_win, NULL, GTTX_Text, file_part (path), TAG_DONE);
            gui_status ("ROM set - hard reset to use it");
        }
        break;
    case GID_LOADCFG:
        if (gui_asl ("Configuration to load", 0, 0, 0, path, sizeof path)) {
            int type = 0;
            if (cfgfile_load (&changed_prefs, path, &type)) {
                show_page (PAGE_SETTINGS);
                gui_status ("config loaded - hard reset to apply");
            } else
                gui_status ("config load failed");
        }
        break;
    case GID_SAVECFG:
        if (gui_asl ("Save configuration as", 0, 0, 1, path, sizeof path))
            gui_status (cfgfile_save (&currprefs, path, 0) ? "config saved" : "config save failed");
        break;
    }
}

static void gui_events (void)
{
    struct IntuiMessage *im;
    int close = 0;

    while (gui_win && (im = GT_GetIMsg (gui_win->UserPort))) {
        ULONG class = im->Class;
        UWORD code  = im->Code;
        UWORD qual  = im->Qualifier;
        int   id    = 0;

        if (class == IDCMP_GADGETUP || class == IDCMP_GADGETDOWN)
            id = ((struct Gadget *) im->IAddress)->GadgetID;
        if (class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh (gui_win);
            GT_EndRefresh (gui_win, TRUE);
        }
        GT_ReplyIMsg (im);

        switch (class) {
        case IDCMP_CLOSEWINDOW:
            close = 1;
            break;
        case IDCMP_RAWKEY:
            if (code == 0x45)                   /* Esc */
                close = 1;
            else if (code == 0x35 && (qual & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT))
                     == (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT))
                close = 1;                      /* Ctrl+LAlt+B */
            break;
        case IDCMP_GADGETUP:
            gui_gadget (id, (WORD) code);
            break;
        }
        if (close || gui_pending_toggle)
            break;
    }
    if (close && gui_win)
        gui_close ();
}

/****************************************************************************/

void gui_display (int shortcut)
{
    switch (shortcut) {

	case 0:
	case 1:
	case 2:
	case 3:
	    do_file_dialog (FILEDIALOG_INSERT_DF0 + shortcut);
	    break;
	case 4:
	    do_file_dialog (FILEDIALOG_LOAD_STATE);
	    break;
	case 5:
	    do_file_dialog (FILEDIALOG_SAVE_STATE);
	    break;
	default:
	    ;
    }
}

/****************************************************************************/

void gui_message (const char *format,...)
{
    char msg[2048];
    va_list parms;
    struct EasyStruct req;
    struct Window *win;

    va_start (parms,format);
    vsprintf ( msg, format, parms);
    va_end (parms);

    /* Find this task's default window */
    win = ((struct Process *) FindTask (NULL))->pr_WindowPtr;
    if (win == (struct Window *)-1)
	win = 0;

    req.es_StructSize   = sizeof req;
    req.es_Flags        = 0;
    req.es_Title = (STRPTR) PACKAGE_NAME " Information";
    req.es_TextFormat = (STRPTR) msg;
    req.es_GadgetFormat = (STRPTR) "Okay";
    EasyRequest (win, &req, NULL, NULL);

    write_log (msg);
}
