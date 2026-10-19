/* Future Retro Fusion */
/*
 * The host's real floppy drives as disk images (od-amiga/hostdisk.c):
 * "realdf0" to "realdf3" open the host's DF0: to DF3: through
 * trackdisk.device; zfile.c hands an emulated drive's reads and writes on.
 */

struct hostdisk;

extern int hostdisk_unit (const char *name);    /* realdf<n>: n, else -1 */
extern int hostdisk_present (int unit);         /* a disk in that drive */
extern struct hostdisk *hostdisk_open (int unit, int write);
extern void hostdisk_close (struct hostdisk *hd);
extern int hostdisk_size (struct hostdisk *hd);
extern size_t hostdisk_read (struct hostdisk *hd, int offset, void *buf, size_t len);
extern size_t hostdisk_write (struct hostdisk *hd, int offset, const void *buf, size_t len);
extern void hostdisk_tick (void);               /* every frame: flush, motor, disk changes */
