/* Future Retro Fusion */
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Hardfile emulation for AmigaOS (replaces hardfile_unix.c there).
  *
  * dos.library Open/Read/Write, and for images beyond 2 GB (an AGS image is
  * ~30 GB) the 64-bit packets of the file's handler, AmigaOS 4 style:
  * ACTION_GET_FILE_SIZE64 and ACTION_CHANGE_FILE_POSITION64, which PFS3,
  * WinUAE's directory filesystem and others answer although dos.library
  * on OS 3.x has no 64-bit calls. Without them an image works up to 2 GB.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "filesys.h"

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define ACTION_CHANGE_FILE_POSITION64  8001
#define ACTION_GET_FILE_SIZE64         8004
#define DP64_INIT                      -3

/* the AmigaOS 4 64-bit packet: offsets as WinUAE's filesys.cpp reads them
   (Type 8, Res0 12, Res2 16, Res1 24, Arg1 32, Arg2 40, Arg3 48) */
struct DosPacket64 {
    struct Message *dp_Link;
    struct MsgPort *dp_Port;
    LONG dp_Type;
    LONG dp_Res0;               /* DP64_INIT: this is a 64-bit packet */
    LONG dp_Res2;
    LONG dp_Pad1;
    LONG dp_Res1_hi, dp_Res1_lo;
    LONG dp_Arg1;
    LONG dp_Pad2;
    LONG dp_Arg2_hi, dp_Arg2_lo;
    LONG dp_Arg3;
    LONG dp_Arg4;
    LONG dp_Arg5_hi, dp_Arg5_lo;
};

struct StandardPacket64 {
    struct Message sp_Msg;
    struct DosPacket64 sp_Pkt;
};

struct amigahdf {
    BPTR fh;
    int has64;                  /* the handler takes the 64-bit packets */
};

#define AHDF(hfd) ((struct amigahdf *) (hfd)->handle)

/* send a 64-bit packet to the file's handler: 1 and Res1 (64 bits) in *res
   when it worked, 0 on failure or when the handler doesn't know the packet.
   (Success is Res2 == 0: a seek answers DOSTRUE, i.e. -1, in Res1.) */
static int packet64 (BPTR fh, LONG type, uae_s64 arg2, LONG arg3, uae_s64 *res)
{
    struct FileHandle *f = (struct FileHandle *) BADDR (fh);
    struct MsgPort *port = CreateMsgPort ();
    struct StandardPacket64 *sp;
    int ok = 0;

    if (!port)
	return 0;
    sp = AllocVec (sizeof *sp, MEMF_PUBLIC | MEMF_CLEAR);
    if (sp && f->fh_Type) {
	sp->sp_Msg.mn_Node.ln_Name = (char *) &sp->sp_Pkt;
	sp->sp_Pkt.dp_Link = &sp->sp_Msg;
	sp->sp_Pkt.dp_Port = port;
	sp->sp_Pkt.dp_Type = type;
	sp->sp_Pkt.dp_Res0 = DP64_INIT;
	sp->sp_Pkt.dp_Arg1 = f->fh_Arg1;
	sp->sp_Pkt.dp_Arg2_hi = (LONG) (arg2 >> 32);
	sp->sp_Pkt.dp_Arg2_lo = (LONG) arg2;
	sp->sp_Pkt.dp_Arg3 = arg3;
	PutMsg (f->fh_Type, &sp->sp_Msg);
	WaitPort (port);
	GetMsg (port);
	/* a handler that knows the packet answers in Res0/Res1 as 64-bit;
	   one that doesn't leaves Res0 alone and fails (ERROR_ACTION_NOT_KNOWN) */
	if (sp->sp_Pkt.dp_Res0 == DP64_INIT && sp->sp_Pkt.dp_Res2 == 0) {
	    if (res)
		*res = ((uae_s64) sp->sp_Pkt.dp_Res1_hi << 32) | (uae_u32) sp->sp_Pkt.dp_Res1_lo;
	    ok = 1;
	}
    }
    FreeVec (sp);
    DeleteMsgPort (port);
    return ok;
}

static int hdf_seek (struct hardfiledata *hfd, uae_u64 offset)
{
    struct amigahdf *a = AHDF (hfd);

    if (offset >= hfd->size) {
	write_log ("hd: seek out of bounds (0x%llx >= 0x%llx)\n", offset, hfd->size);
	return -1;
    }
    offset += hfd->offset;
    if (a->has64)
	return packet64 (a->fh, ACTION_CHANGE_FILE_POSITION64, offset, OFFSET_BEGINNING, 0) ? 0 : -1;
    if (offset >= 0x80000000ULL) {
	static int told;
	if (!told++)
	    write_log ("hd: '%s' is beyond 2 GB and its filesystem has no 64-bit seek\n",
		       hfd->device_name);
	return -1;
    }
    return Seek (a->fh, (LONG) offset, OFFSET_BEGINNING) == -1 ? -1 : 0;
}

int hdf_open (struct hardfiledata *hfd, const char *name)
{
    struct amigahdf *a = calloc (1, sizeof *a);
    const char *p;
    uae_s64 size;

    if (!a)
	return 0;
    a->fh = Open ((STRPTR) name, hfd->readonly ? MODE_OLDFILE : MODE_OLDFILE);
    if (!a->fh) {
	free (a);
	return 0;
    }
    hfd->handle = a;
    hfd->cache = 0;
    strncpy (hfd->device_name, name, sizeof hfd->device_name - 1);
    hfd->device_name[sizeof hfd->device_name - 1] = 0;

    p = strrchr (name, '/');
    if (!p)
	p = strrchr (name, ':');
    strcpy (hfd->vendor_id, "UAE");
    strncpy (hfd->product_id, p ? p + 1 : name, 15);
    strcpy (hfd->product_rev, "0.3");

    /* the size: 64-bit if the handler can, else the 32-bit way */
    if (packet64 (a->fh, ACTION_GET_FILE_SIZE64, 0, 0, &size) && size >= 0) {
	a->has64 = 1;
    } else {
	Seek (a->fh, 0, OFFSET_END);
	size = (uae_u32) Seek (a->fh, 0, OFFSET_BEGINNING);
    }
    hfd->size = hfd->size2 = size;
    hfd->blocksize = 512;
    write_log ("hd: '%s' %llu MB%s\n", name, (unsigned long long) (size >> 20),
	       a->has64 ? ", 64-bit access" : "");
    return 1;
}

int hdf_dup (struct hardfiledata *dhfd, const struct hardfiledata *shfd)
{
    /* the copy gets its own handle on the same file (filesys.c calls this
       with dhfd == shfd, after copying the struct - read shfd first) */
    struct hardfiledata tmp = *shfd;
    int had = shfd->handle != 0;

    dhfd->handle = 0;
    if (had && tmp.device_name[0] && hdf_open (&tmp, tmp.device_name)) {
	dhfd->handle = tmp.handle;
	dhfd->size = dhfd->size2 = tmp.size;
    }
    return 0;
}

void hdf_close (struct hardfiledata *hfd)
{
    struct amigahdf *a = AHDF (hfd);

    if (a) {
	if (a->fh)
	    Close (a->fh);
	free (a);
    }
    hfd->handle = 0;
}

int hdf_read (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    static int logged;
    int sk, n = 0;

    hfd->cache_valid = 0;
    if (!hfd->handle)
	return 0;
    sk = hdf_seek (hfd, offset);
    if (!sk)
	n = Read (AHDF (hfd)->fh, buffer, len);
    if (logged < 6) {           /* the first reads, for the log */
	const uae_u8 *b = buffer;
	logged++;
	write_log ("hd: read 0x%llx+%d: seek %s, %d bytes, %02x%02x%02x%02x\n", offset, len,
		   sk ? "FAILED" : "ok", n, b[0], b[1], b[2], b[3]);
    }
    return n;
}

int hdf_write (struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    hfd->cache_valid = 0;
    if (!hfd->handle || hfd->readonly || hdf_seek (hfd, offset))
	return 0;
    return Write (AHDF (hfd)->fh, buffer, len);
}
