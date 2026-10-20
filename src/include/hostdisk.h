/* Future Retro Fusion */
/*
 * The host's real floppy drives as disk images (od-amiga/hostdisk.c):
 * "realdf0" to "realdf3" open the host's DF0: to DF3: through
 * trackdisk.device; zfile.c hands an emulated drive's reads and writes on.
 * "rawdf0" to "rawdf3" are the same drives read as raw MFM tracks (copy-
 * protected disks), read-only: disk.c takes the tracks from
 * hostdisk_rawtrack.
 */

struct hostdisk;
struct zfile;

extern int hostdisk_unit (const char *name);    /* realdf<n>/rawdf<n>: n, else -1 */
extern int hostdisk_israw (const char *name);   /* rawdf<n> */
extern int hostdisk_present (int unit);         /* a disk in that drive */
extern struct hostdisk *hostdisk_open (int unit, int write, int raw);
extern void hostdisk_close (struct hostdisk *hd);
extern int hostdisk_size (struct hostdisk *hd);
extern size_t hostdisk_read (struct hostdisk *hd, int offset, void *buf, size_t len);
extern size_t hostdisk_write (struct hostdisk *hd, int offset, const void *buf, size_t len);
extern int hostdisk_rawtrack (struct hostdisk *hd, int track, uae_u16 *mfm, int maxwords);
extern void hostdisk_tick (void);               /* every frame: flush, motor, disk changes */

extern struct hostdisk *zfile_hostdisk (struct zfile *f);  /* zfile.c: a real drive's, else 0 */
