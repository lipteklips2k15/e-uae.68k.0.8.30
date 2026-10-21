 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Library of functions to make emulated filesystem as independent as
  * possible of the host filesystem's capabilities.
  *
  * Copyright 1999 Bernd Schmidt
  * Copyright 2025 Future Retro Fusion
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "filesys.h"
#include "autoconf.h"
#include "fsusage.h"

#include <time.h>
#ifdef TARGET_AMIGAOS
#include <dos/dos.h>
#include <proto/dos.h>
#endif
#include "scsidev.h"
#include "fsdb.h"

/* The on-disk format is as follows:
 * Offset 0, 1 byte, valid
 * Offset 1, 4 bytes, mode
 * Offset 5, 257 bytes, aname
 * Offset 263, 257 bytes, nname
 * Offset 519, 81 bytes, comment
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Index entry: points into tmpbuf; lifetime tied to tmpbuf */
typedef struct { const char* aname; int off; } fsdb_idx_t;

static int fsdb_cmp_idx_aname(const void *pa, const void *pb)
{
    const fsdb_idx_t *a = (const fsdb_idx_t *)pa;
    const fsdb_idx_t *b = (const fsdb_idx_t *)pb;
    return strcmp(a->aname, b->aname);
}

struct fsdb_key_t { const char* s; };

static int fsdb_cmp_key_to_idx(const void *pkey, const void *pelem)
{
    const struct fsdb_key_t *k = (const struct fsdb_key_t *)pkey;
    const fsdb_idx_t *e       = (const fsdb_idx_t *)pelem;
    return strcmp(k->s, e->aname);
}

#define TRACING_ENABLED 0
#if TRACING_ENABLED
#define TRACE(x)	do { write_log x; } while(0)
#else
#define TRACE(x)
#endif

char *nname_begin (char *nname)
{
    char *p = strrchr (nname, FSDB_DIR_SEPARATOR);
    if (p)
	return p + 1;
    return nname;
}

#if 1 //ndef _WIN32
/* Find the name REL in directory DIRNAME.  If we find a file that
 * has exactly the same name, return REL.  If we find a file that
 * has the same name when compared case-insensitively, return a
 * malloced string that contains the name we found.  If no file
 * exists that compares equal to REL, return 0.  */
struct fsdb_perf fsdb_perf;

/*
 * Caches. Every host call is slow on a PiStorm (opendir, stat, opening a
 * _UAEFSDB.___ that isn't there: each goes through the host's DOS and its
 * storage), and a name lookup made several, a directory listing several
 * per entry.
 * - a directory's host entries are read once (AmigaOS: Examine/ExNext,
 *   which gives type, protection, size and date too) for name lookups,
 *   ExNext listings and the attributes of what is found. A packet that
 *   changes anything (fsdb_changed, from filesys.c) makes all of them
 *   stale; a lookup that misses reads the directory again when it is
 *   DC_FRESH seconds old (files put there from outside), and attributes and
 *   Examine data come from it only while it is that fresh.
 * - a directory's _UAEFSDB.___ is read once, or "none" remembered; writing
 *   it drops the copy.
 */
#define DC_FRESH 2

struct fsdb_dent {
    char *name;
    int dir;
    uae_u32 prot, size, blocks, days, mins, ticks;
};

struct fsdb_dircache {
    int n, max;
    unsigned long gen;
    time_t when;
    struct fsdb_dent *e;
};

static unsigned long fsdb_gen = 1;

void fsdb_changed (void)
{
    fsdb_gen++;
}

static void dc_free (struct fsdb_dircache *dc)
{
    int i;

    if (!dc)
	return;
    for (i = 0; i < dc->n; i++)
	free (dc->e[i].name);
    free (dc->e);
    free (dc);
}

static struct fsdb_dent *dc_add (struct fsdb_dircache *dc, const char *name)
{
    struct fsdb_dent *e;

    if (dc->n == dc->max) {
	int max = dc->max ? dc->max * 2 : 32;
	struct fsdb_dent *ne = realloc (dc->e, max * sizeof *ne);
	if (!ne)
	    return 0;
	dc->e = ne;
	dc->max = max;
    }
    e = &dc->e[dc->n];
    memset (e, 0, sizeof *e);
    if (!(e->name = my_strdup (name)))
	return 0;
    dc->n++;
    fsdb_perf.dirents++;
    return e;
}

static struct fsdb_dircache *dc_read (const char *path)
{
    struct fsdb_dircache *dc = calloc (1, sizeof *dc);

    if (!dc)
	return 0;
    dc->gen = fsdb_gen;
    dc->when = time (NULL);
    fsdb_perf.dirscans++;
#ifdef TARGET_AMIGAOS
    {
	BPTR lock = Lock ((STRPTR) path, SHARED_LOCK);
	struct FileInfoBlock *fib = AllocDosObject (DOS_FIB, NULL);

	if (lock && fib && Examine (lock, fib)) {
	    while (ExNext (lock, fib)) {
		struct fsdb_dent *e = dc_add (dc, (char *) fib->fib_FileName);
		if (!e)
		    break;
		e->dir = fib->fib_DirEntryType > 0;
		e->prot = fib->fib_Protection;
		e->size = fib->fib_Size;
		e->blocks = fib->fib_NumBlocks;
		e->days = fib->fib_Date.ds_Days;
		e->mins = fib->fib_Date.ds_Minute;
		e->ticks = fib->fib_Date.ds_Tick;
	    }
	}
	if (fib)
	    FreeDosObject (DOS_FIB, fib);
	if (lock)
	    UnLock (lock);
    }
#else
    {
	DIR *d = opendir (path);
	struct dirent *de;

	if (d) {
	    while ((de = readdir (d)) != 0)
		if (!dc_add (dc, de->d_name))
		    break;
	    closedir (d);
	}
    }
#endif
    return dc;
}

static int dc_fresh (const struct fsdb_dircache *dc)
{
    return dc && dc->gen == fsdb_gen && time (NULL) - dc->when < DC_FRESH;
}

/* the directory's entries; read again when stale (and when old, if asked) */
static struct fsdb_dircache *dc_get (a_inode *dir, int refresh_old)
{
    struct fsdb_dircache *dc = dir->dcache;

    if (dc && (dc->gen != fsdb_gen || (refresh_old && time (NULL) - dc->when >= DC_FRESH))) {
	dc_free (dc);
	dc = dir->dcache = 0;
    }
    if (!dc)
	dc = dir->dcache = dc_read (dir->nname);
    return dc;
}

/* the entry called name: the exact name, else one that differs in case */
static struct fsdb_dent *dc_find (struct fsdb_dircache *dc, const char *name, int exact)
{
    int i;

    if (!dc)
	return 0;
    for (i = 0; i < dc->n; i++)
	if (strcmp (dc->e[i].name, name) == 0)
	    return &dc->e[i];
    if (!exact)
	for (i = 0; i < dc->n; i++)
	    if (strcasecmp (dc->e[i].name, name) == 0)
		return &dc->e[i];
    return 0;
}

/* Find the name REL in directory BASE.  If we find a file that
 * has exactly the same name, return REL.  If we find a file that
 * has the same name when compared case-insensitively, return a
 * malloced string that contains the name we found.  If no file
 * exists that compares equal to REL, return 0.  */
char *fsdb_search_dir (a_inode *base, char *rel)
{
    struct fsdb_dircache *dc = dc_get (base, 0);
    struct fsdb_dent *e = dc_find (dc, rel, 0);

    if (!e && dc && !dc_fresh (dc)) {   /* maybe put there since */
	dc = dc_get (base, 1);
	e = dc_find (dc, rel, 0);
    }
    if (!e)
	return 0;
    return strcmp (e->name, rel) == 0 ? rel : my_strdup (e->name);
}

/* ExNext: the directory read as it is now (unless just read) */
int fsdb_dir_read (a_inode *dir)
{
    struct fsdb_dircache *dc = dc_get (dir, 1);
    return dc ? dc->n : 0;
}

const char *fsdb_dir_name (a_inode *dir, int i)
{
    return dir->dcache && i < dir->dcache->n ? dir->dcache->e[i].name : 0;
}

/* attributes from a fresh cache of the parent directory, no stat */
int fsdb_cached_attrs (a_inode *base, a_inode *aino)
{
    struct fsdb_dent *e;

    if (!base || !dc_fresh (base->dcache))
	return 0;
    e = dc_find (base->dcache, nname_begin (aino->nname), 1);
    if (!e)
	return 0;
    aino->dir = e->dir;
    aino->amigaos_mode = e->prot & (A_FIBF_READ | A_FIBF_WRITE | A_FIBF_EXECUTE);
    return 1;
}

/* Examine's size and date from a fresh cache of the parent directory */
int fsdb_cached_info (a_inode *aino, uae_u32 *size, uae_u32 *blocks,
		      uae_u32 *days, uae_u32 *mins, uae_u32 *ticks)
{
    struct fsdb_dent *e;

    if (!aino->parent || !dc_fresh (aino->parent->dcache))
	return 0;
    e = dc_find (aino->parent->dcache, nname_begin (aino->nname), 1);
    if (!e)
	return 0;
    *size = e->size;
    *blocks = e->blocks;
    *days = e->days;
    *mins = e->mins;
    *ticks = e->ticks;
    return 1;
}

#define DB_RECSZ (1 + 4 + 257 + 257 + 81)

static FILE *get_fsdb (a_inode *dir, const char *mode);

/* the directory's _UAEFSDB.___ into dir->dbdata (1), or there is none (0) */
static int db_load (a_inode *dir)
{
    if (dir->dbstate == 0) {
	FILE *f = get_fsdb (dir, "rb");
	dir->dbstate = 1;
	if (f) {
	    long len;
	    fseek (f, 0, SEEK_END);
	    len = ftell (f);
	    fseek (f, 0, SEEK_SET);
	    len -= len % DB_RECSZ;
	    dir->dbdata = malloc (len > 0 ? len : 1);
	    if (dir->dbdata && (len == 0 || fread (dir->dbdata, 1, len, f) == (size_t) len)) {
		dir->dblen = len;
		dir->dbstate = 2;
	    } else {
		free (dir->dbdata);
		dir->dbdata = 0;
		dir->dbstate = 0;       /* try again next time */
	    }
	    fclose (f);
	}
    }
    return dir->dbstate == 2;
}

static void db_drop (a_inode *dir)
{
    free (dir->dbdata);
    dir->dbdata = 0;
    dir->dblen = 0;
    dir->dbstate = 0;
}

void fsdb_free_caches (a_inode *aino)
{
    dc_free (aino->dcache);
    aino->dcache = 0;
    db_drop (aino);
}
#endif

static FILE *get_fsdb (a_inode *dir, const char *mode)
{
    char *n;
    FILE *f;

    n = build_nname (dir->nname, FSDB_FILE);
    fsdb_perf.fsdbopens++;
    f = fopen (n, mode);
    free (n);
    return f;
}

static void kill_fsdb (a_inode *dir)
{
    char *n = build_nname (dir->nname, FSDB_FILE);
    db_drop (dir);
    unlink (n);
    free (n);
}

static void fsdb_fixup (FILE *f, char *buf, int size, a_inode *base)
{
    char *nname;
    int ret;

    if (buf[0] == 0)
	return;
    nname = build_nname (base->nname, buf + 5 + 257);
    ret = fsdb_exists (nname);
    if (ret) {
	free (nname);
	return;
    }
    TRACE (("uaefsdb '%s' deleted\n", nname));
    /* someone deleted this file/dir outside of emulation.. */
    buf[0] = 0;
    free (nname);
}

/* Prune the db file the first time this directory is opened in a session.  */
void fsdb_clean_dir (a_inode *dir)
{
    char buf[1 + 4 + 257 + 257 + 81];
    char *n;
    FILE *f;
    off_t pos1 = 0, pos2;

    if (!db_load (dir))                 /* none: nothing to clean */
	return;
    db_drop (dir);                      /* about to be rewritten */
    n = build_nname (dir->nname, FSDB_FILE);
    f = fopen (n, "r+b");
    if (f == 0) {
	free (n);
	return;
    }
    for (;;) {
	pos2 = ftell (f);
	if (fread (buf, 1, sizeof buf, f) < sizeof buf)
	    break;
	fsdb_fixup (f, buf, sizeof buf, dir);
	if (buf[0] == 0)
	    continue;
	if (pos1 != pos2) {
	    fseek (f, pos1, SEEK_SET);
	    fwrite (buf, 1, sizeof buf, f);
	    fseek (f, pos2 + sizeof buf, SEEK_SET);
	}
	pos1 += sizeof buf;
    }
    fclose (f);
    truncate (n, pos1);
    free (n);
}

static a_inode *aino_from_buf (a_inode *base, char *buf, long off)
{
    uae_u32 mode;
    a_inode *aino = (a_inode *) xcalloc (sizeof (a_inode), 1);

    mode = do_get_mem_long ((uae_u32 *)(buf + 1));
    buf += 5;
    aino->aname = my_strdup (buf);
    buf += 257;
    aino->nname = build_nname (base->nname, buf);
    buf += 257;
    aino->comment = *buf != '\0' ? my_strdup (buf) : 0;
    fsdb_fill_file_attrs (base, aino);
    aino->amigaos_mode = mode;
    aino->has_dbentry = 1;
    aino->dirty = 0;
    aino->db_offset = off;
    return aino;
}

/* the database looked up in its cached copy */
a_inode *fsdb_lookup_aino_aname (a_inode *base, const char *aname)
{
    long off;

    if (!db_load (base))
	return 0;
    for (off = 0; off + DB_RECSZ <= base->dblen; off += DB_RECSZ) {
	char *buf = base->dbdata + off;
	if (buf[0] != 0 && same_aname (buf + 5, aname))
	    return aino_from_buf (base, buf, off);
    }
    return 0;
}

a_inode *fsdb_lookup_aino_nname (a_inode *base, const char *nname)
{
    long off;

    if (!db_load (base))
	return 0;
    for (off = 0; off + DB_RECSZ <= base->dblen; off += DB_RECSZ) {
	char *buf = base->dbdata + off;
	if (buf[0] != 0 && strcmp (buf + 5 + 257, nname) == 0)
	    return aino_from_buf (base, buf, off);
    }
    return 0;
}

int fsdb_used_as_nname (a_inode *base, const char *nname)
{
    long off;

    if (!db_load (base))
	return 0;
    for (off = 0; off + DB_RECSZ <= base->dblen; off += DB_RECSZ) {
	char *buf = base->dbdata + off;
	if (buf[0] != 0 && strcmp (buf + 5 + 257, nname) == 0)
	    return 1;
    }
    return 0;
}

static int needs_dbentry (a_inode *aino)
{
    const char *nn_begin;

    if (aino->deleted)
	return 0;

    if (! fsdb_mode_representable_p (aino) || aino->comment != 0)
	return 1;

    nn_begin = nname_begin (aino->nname);
    return strcmp (nn_begin, aino->aname) != 0;
}

static void write_aino (FILE *f, a_inode *aino)
{
    char buf[1 + 4 + 257 + 257 + 81];
    buf[0] = aino->needs_dbentry;
    do_put_mem_long ((uae_u32 *)(buf + 1), aino->amigaos_mode);
    strncpy (buf + 5, aino->aname, 256);
    buf[5 + 256] = '\0';
    strncpy (buf + 5 + 257, nname_begin (aino->nname), 256);
    buf[5 + 257 + 256] = '\0';
    strncpy (buf + 5 + 2*257, aino->comment ? aino->comment : "", 80);
    buf[5 + 2 * 257 + 80] = '\0';
    aino->db_offset = ftell (f);
    fwrite (buf, 1, sizeof buf, f);
    aino->has_dbentry = aino->needs_dbentry;
    TRACE (("%d '%s' '%s' written\n", aino->db_offset, aino->aname, aino->nname));
}

/* Write back the db file for a directory.  */

void fsdb_dir_writeback (a_inode *dir)
{
    FILE *f;
    int changes_needed = 0;
    int entries_needed = 0;
    a_inode *aino;
    char *tmpbuf;
    int size, i;

    TRACE (("fsdb writeback %s\n", dir->aname));
    db_drop (dir);                      /* the file changes: read anew */
    /* First pass: clear dirty bits where unnecessary, and see if any work
     * needs to be done.  */
    for (aino = dir->child; aino; aino = aino->sibling) {
/*
	int old_needs_dbentry = aino->needs_dbentry || aino->has_dbentry;
	aino->needs_dbentry = needs_dbentry (aino);
	entries_needed |= aino->has_dbentry | aino->needs_dbentry;
*/
	int old_needs_dbentry = aino->has_dbentry;
	int need = needs_dbentry (aino);
	aino->needs_dbentry = need;
	entries_needed |= need;
	if (! aino->dirty)
	    continue;
	if (! aino->needs_dbentry && ! old_needs_dbentry)
	    aino->dirty = 0;
	else
	    changes_needed = 1;
    }
    if (! entries_needed) {
	kill_fsdb (dir);
	TRACE (("fsdb removed\n"));
	return;
    }

    if (! changes_needed) {
	TRACE (("not modified\n"));
	return;
    }

    f = get_fsdb (dir, "r+b");
    if (f == 0) {
        if (/*(currprefs.filesys_custom_uaefsdb && (dir->volflags & MYVOLUMEINFO_STREAMS)) ||*/ currprefs.filesys_no_uaefsdb) {
            for (aino = dir->child; aino; aino = aino->sibling) {
                aino->dirty = 0;
                aino->has_dbentry = 0;
                aino->needs_dbentry = 0;
            }
            return;
        }
        f = get_fsdb (dir, "w+b");
        if (f == 0) {
            TRACE (("failed\n"));
            return;
        }
    }

   /* Larger stdio buffer to cut syscall chatter. */
setvbuf(f, NULL, _IOFBF, 16*1024);

/* Snapshot old file to memory once. */
fseek(f, 0, SEEK_END);
size = (int)ftell(f);
fseek(f, 0, SEEK_SET);
tmpbuf = (size > 0) ? (char*)malloc(size) : NULL;
if (size > 0 && tmpbuf)
    fread(tmpbuf, 1, size, f);
TRACE(("**** updating '%s' %d\n", dir->aname, size));

/* Build an index of {aname->offset} from tmpbuf so we avoid O(N^2). */
{
    int recsz = 1 + 4 + 257 + 257 + 81;   /* record size in bytes */
    int nrecs = (size > 0) ? (size / recsz) : 0;
    fsdb_idx_t* idx = NULL;
    int iidx = 0;

    if (nrecs > 0 && tmpbuf) {
        int off;
        idx = (fsdb_idx_t*)malloc(nrecs * sizeof(fsdb_idx_t));
        if (idx) {
            for (off = 0; off + recsz <= size; off += recsz) {
                char *rec = tmpbuf + off;
                if (rec[0] == 0)
                    continue;               /* empty slot */
                idx[iidx].aname = rec + 5;  /* aname at +5 */
                idx[iidx].off   = off;
                iidx++;
            }
            if (iidx > 1)
                qsort(idx, iidx, sizeof(fsdb_idx_t), fsdb_cmp_idx_aname);
        }
    }

    /* Write back dirty children using the index (bsearch) or append. */
    aino = dir->child;
    while (aino) {
        if (aino->dirty) {
            long target_off = -1;
            aino->dirty = 0;

            if (iidx > 0 && idx) {
                struct fsdb_key_t key;
                fsdb_idx_t *hit;
                key.s = aino->aname;
                hit = (fsdb_idx_t*)bsearch(&key, idx, iidx,
                                           sizeof(fsdb_idx_t),
                                           fsdb_cmp_key_to_idx);
                if (hit)
                    target_off = hit->off;
            }

            if (target_off < 0) {
                fseek(f, 0, SEEK_END);
                aino->has_dbentry = 1;
            } else {
                fseek(f, target_off, SEEK_SET);
                aino->has_dbentry = 1;
                aino->db_offset = (long)target_off;
            }
            write_aino(f, aino); /* existing helper */
        }
        aino = aino->sibling;
    }

    TRACE(("end\n"));
    fclose(f);
    if (tmpbuf) free(tmpbuf);
    if (idx)    free(idx);
}
}
