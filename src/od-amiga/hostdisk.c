/* Future Retro Fusion */
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * hostdisk.c: the host's real floppy drives as disk images. "realdf0" to
  * "realdf3" open DF0: to DF3: through trackdisk.device; zfile.c passes an
  * emulated drive's reads and writes on, so disk.c sees an ordinary ADF.
  *
  * - reads come a track at a time (one track cached)
  * - writes go into trackdisk's track buffer, flushed when the drive has
  *   been idle a moment; the motor goes off after a couple of seconds
  * - the host's DOS keeps out of the drive while it is ours (Inhibit)
  * - a write-protected disk is write-protected in the emulator too
  * - taking the real disk out or putting one in does the same with the
  *   emulated drive (hostdisk_tick)
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <exec/memory.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "options.h"
#include "disk.h"
#include "hostdisk.h"

#define FLUSH_FRAMES  25        /* idle this long: write the track out */
#define MOTOR_FRAMES  100       /* ... and this long: motor off */
#define WATCH_FRAMES  50        /* look for disk changes this often */

struct hostdisk {
    struct MsgPort *port;
    struct IOExtTD *io;
    int unit;
    ULONG changenum;
    int size, tracksize;
    int protect;
    uae_u8 *track;              /* the cached track */
    int track_no;               /* its number, -1 none */
    int dirty;                  /* written, not flushed */
    int motor;
    int idle;                   /* frames since the last access */
};

static struct hostdisk *open_disks[8];
static int inhibits[4];

/* an emulated drive on a real one: noticing disk changes */
static struct {
    int unit;                   /* -1: not watching */
    struct MsgPort *port;
    struct IOExtTD *io;
    ULONG changenum;
    int ejected;                /* we took the emulated disk out */
} watch[4] = { { -1 }, { -1 }, { -1 }, { -1 } };

static int td_open (int unit, struct MsgPort **port, struct IOExtTD **io)
{
    *io = 0;
    *port = CreateMsgPort ();
    if (!*port)
        return 0;
    *io = (struct IOExtTD *) CreateIORequest (*port, sizeof (struct IOExtTD));
    if (*io && !OpenDevice ((STRPTR) TD_NAME, unit, (struct IORequest *) *io, 0))
        return 1;
    if (*io)
        DeleteIORequest ((struct IORequest *) *io);
    DeleteMsgPort (*port);
    *io = 0;
    *port = 0;
    return 0;
}

static void td_close (struct MsgPort *port, struct IOExtTD *io)
{
    CloseDevice ((struct IORequest *) io);
    DeleteIORequest ((struct IORequest *) io);
    DeleteMsgPort (port);
}

static LONG td_cmd (struct IOExtTD *io, UWORD cmd, ULONG count, ULONG offset, ULONG length, APTR data)
{
    io->iotd_Req.io_Command = cmd;
    io->iotd_Req.io_Flags   = 0;
    io->iotd_Req.io_Offset  = offset;
    io->iotd_Req.io_Length  = length;
    io->iotd_Req.io_Data    = data;
    io->iotd_Count          = count;
    return DoIO ((struct IORequest *) io);
}

/* the host's DOS out of the drive while we have it (counted: disk.c opens
   an image more than once) */
static void dos_inhibit (int unit, int on)
{
    char dev[8];

    if (on ? inhibits[unit]++ : --inhibits[unit])
        return;
    sprintf (dev, "DF%d:", unit);
    Inhibit ((STRPTR) dev, on ? DOSTRUE : DOSFALSE);
}

int hostdisk_unit (const char *name)
{
    if (name && !strncasecmp (name, "realdf", 6) && name[6] >= '0' && name[6] <= '3' && !name[7])
        return name[6] - '0';
    return -1;
}

int hostdisk_present (int unit)
{
    struct MsgPort *port;
    struct IOExtTD *io;
    int present = 0;

    if (!td_open (unit, &port, &io))
        return 0;
    if (!td_cmd (io, TD_CHANGESTATE, 0, 0, 0, 0))
        present = io->iotd_Req.io_Actual == 0;
    td_close (port, io);
    return present;
}

struct hostdisk *hostdisk_open (int unit, int write)
{
    struct hostdisk *hd = calloc (1, sizeof *hd);
    struct DriveGeometry geo;
    int i;

    if (!hd)
        return 0;
    hd->unit = unit;
    hd->track_no = -1;
    if (!td_open (unit, &hd->port, &hd->io)) {
        write_log ("HOSTDISK: no trackdisk.device unit %d\n", unit);
        free (hd);
        return 0;
    }
    if (td_cmd (hd->io, TD_CHANGESTATE, 0, 0, 0, 0) || hd->io->iotd_Req.io_Actual) {
        write_log ("HOSTDISK: no disk in DF%d:\n", unit);
        goto fail;
    }
    td_cmd (hd->io, TD_CHANGENUM, 0, 0, 0, 0);
    hd->changenum = hd->io->iotd_Req.io_Actual;
    td_cmd (hd->io, TD_PROTSTATUS, 0, 0, 0, 0);
    hd->protect = hd->io->iotd_Req.io_Actual != 0;
    if (write && hd->protect)
        goto fail;                      /* disk.c asks again, read-only */

    hd->size = 80 * 2 * 11 * 512;
    hd->tracksize = 11 * 512;
    if (!td_cmd (hd->io, TD_GETGEOMETRY, 0, 0, sizeof geo, &geo)
        && geo.dg_SectorSize == 512 && geo.dg_TrackSectors) {
        hd->size = geo.dg_TotalSectors * geo.dg_SectorSize;
        hd->tracksize = geo.dg_TrackSectors * geo.dg_SectorSize;
    }
    hd->track = AllocVec (hd->tracksize, MEMF_PUBLIC);
    if (!hd->track)
        goto fail;

    for (i = 0; i < 8; i++)
        if (!open_disks[i]) {
            open_disks[i] = hd;
            break;
        }
    dos_inhibit (unit, 1);
    write_log ("HOSTDISK: DF%d: %d bytes, %s\n", unit, hd->size, hd->protect ? "write-protected" : "writable");
    return hd;

fail:
    td_close (hd->port, hd->io);
    free (hd);
    return 0;
}

static void flush (struct hostdisk *hd)
{
    if (hd->dirty) {
        if (td_cmd (hd->io, ETD_UPDATE, hd->changenum, 0, 0, 0))
            write_log ("HOSTDISK: DF%d: write error %d\n", hd->unit, hd->io->iotd_Req.io_Error);
        hd->dirty = 0;
    }
}

static void motor_off (struct hostdisk *hd)
{
    if (hd->motor) {
        td_cmd (hd->io, TD_MOTOR, 0, 0, 0, 0);
        hd->motor = 0;
    }
}

void hostdisk_close (struct hostdisk *hd)
{
    int i;

    if (!hd)
        return;
    flush (hd);
    motor_off (hd);
    for (i = 0; i < 8; i++)
        if (open_disks[i] == hd)
            open_disks[i] = 0;
    dos_inhibit (hd->unit, 0);
    FreeVec (hd->track);
    td_close (hd->port, hd->io);
    free (hd);
}

int hostdisk_size (struct hostdisk *hd)
{
    return hd->size;
}

static void load_track (struct hostdisk *hd, int t)
{
    if (t == hd->track_no)
        return;
    hd->motor = 1;
    if (td_cmd (hd->io, ETD_READ, hd->changenum, t * hd->tracksize, hd->tracksize, hd->track)) {
        static int logged;
        if (logged++ < 20)
            write_log ("HOSTDISK: DF%d: track %d: read error %d\n", hd->unit, t, hd->io->iotd_Req.io_Error);
        memset (hd->track, 0, hd->tracksize);
        hd->track_no = -1;              /* try again next time */
        return;
    }
    hd->track_no = t;
}

size_t hostdisk_read (struct hostdisk *hd, int offset, void *buf, size_t len)
{
    size_t done = 0;

    hd->idle = 0;
    while (done < len && offset < hd->size) {
        int t = offset / hd->tracksize, o = offset % hd->tracksize;
        size_t n = hd->tracksize - o;
        if (n > len - done)
            n = len - done;
        load_track (hd, t);
        memcpy ((uae_u8 *) buf + done, hd->track + o, n);
        done += n;
        offset += n;
    }
    return done;
}

size_t hostdisk_write (struct hostdisk *hd, int offset, const void *buf, size_t len)
{
    hd->idle = 0;
    if (hd->protect || offset < 0 || offset + (int) len > hd->size || ((offset | len) & 511))
        return 0;
    hd->motor = 1;
    if (td_cmd (hd->io, ETD_WRITE, hd->changenum, offset, len, (APTR) buf)) {
        write_log ("HOSTDISK: DF%d: write error %d at %d\n", hd->unit, hd->io->iotd_Req.io_Error, offset);
        return 0;
    }
    hd->dirty = 1;
    /* keep the cached track as the disk is now */
    if (hd->track_no >= 0) {
        int ts = hd->track_no * hd->tracksize;
        if (offset < ts + hd->tracksize && offset + (int) len > ts) {
            int from = offset > ts ? offset : ts;
            int to = offset + (int) len < ts + hd->tracksize ? offset + (int) len : ts + hd->tracksize;
            memcpy (hd->track + (from - ts), (const uae_u8 *) buf + (from - offset), to - from);
        }
    }
    return len;
}

/* the real drive's disk changed: the emulated drive's follows */
static void watch_drives (void)
{
    int k;

    for (k = 0; k < 4; k++) {
        int u = hostdisk_unit (currprefs.df[k]);
        ULONG cn;
        int present;

        if (u < 0) {
            /* not ours any more (another image, or empty and not by us) */
            if (watch[k].io && !(watch[k].ejected && !currprefs.df[k][0])) {
                td_close (watch[k].port, watch[k].io);
                watch[k].io = 0;
                watch[k].unit = -1;
                watch[k].ejected = 0;
            }
            if (!watch[k].io)
                continue;
            u = watch[k].unit;
        }
        if (!watch[k].io) {
            if (!td_open (u, &watch[k].port, &watch[k].io))
                continue;
            watch[k].unit = u;
            td_cmd (watch[k].io, TD_CHANGENUM, 0, 0, 0, 0);
            watch[k].changenum = watch[k].io->iotd_Req.io_Actual;
            continue;
        }
        if (td_cmd (watch[k].io, TD_CHANGENUM, 0, 0, 0, 0))
            continue;
        cn = watch[k].io->iotd_Req.io_Actual;
        if (cn == watch[k].changenum)
            continue;
        watch[k].changenum = cn;
        td_cmd (watch[k].io, TD_CHANGESTATE, 0, 0, 0, 0);
        present = watch[k].io->iotd_Req.io_Actual == 0;
        if (currprefs.df[k][0])
            disk_eject (k);             /* closes our image of the old disk */
        if (present) {
            char name[8];
            sprintf (name, "realdf%d", u);
            strcpy (changed_prefs.df[k], name);
            disk_insert (k, name);
            watch[k].ejected = 0;
            write_log ("HOSTDISK: DF%d: new disk -> emulated DF%d:\n", u, k);
        } else {
            watch[k].ejected = 1;
            write_log ("HOSTDISK: DF%d: disk out -> emulated DF%d: empty\n", u, k);
        }
    }
}

void hostdisk_tick (void)
{
    static int frames;
    int i;

    for (i = 0; i < 8; i++) {
        struct hostdisk *hd = open_disks[i];
        if (!hd)
            continue;
        hd->idle++;
        if (hd->idle == FLUSH_FRAMES)
            flush (hd);
        if (hd->idle == MOTOR_FRAMES)
            motor_off (hd);
    }
    if (++frames >= WATCH_FRAMES) {
        frames = 0;
        watch_drives ();
    }
}
