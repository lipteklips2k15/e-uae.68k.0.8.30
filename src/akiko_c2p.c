/* Future Retro Fusion */
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Akiko's chunky-to-planar converter on its own, without the rest of the
  * CD32 (akiko.c): the ID ($CAFE at $B80002) and the C2P register at
  * $B80038, behind prefs akiko_c2p. Kickstart's graphics.library finds it by
  * the ID and uses it for WriteChunkyPixels(); programs can use it directly.
  *
  * Register behaviour as akiko.c: writes fill eight chunky longs (32 pixels,
  * byte 3 of a long first), the first read converts, reads hand out the
  * eight planar longs (plane 0 first, pixel 0 in bit 31).
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "akiko_c2p.h"

static uae_u32 chunky[8], planar[8];
static int write_pos, read_pos;

static uae_u32 spread[256];     /* bit p of a byte -> bit 4p */

static void c2p_convert (void)
{
    int l, p;

    if (!spread[1]) {
        int v;
        write_log ("AKIKO: C2P in use\n");      /* once: something uses it */
        for (v = 0; v < 256; v++)
            for (p = 0; p < 8; p++)
                if (v & (1 << p))
                    spread[v] |= 1u << (p * 4);
    }
    for (p = 0; p < 8; p++)
        planar[p] = 0;
    for (l = 0; l < 8; l++) {
        uae_u32 c = chunky[l];
        /* nibble p of x: plane p of these 4 pixels, the first in bit 3 */
        uae_u32 x = (spread[c >> 24] << 3) | (spread[(c >> 16) & 0xff] << 2)
                  | (spread[(c >> 8) & 0xff] << 1) | spread[c & 0xff];
        for (p = 0; p < 8; p++)
            planar[p] |= ((x >> (p * 4)) & 15) << (28 - l * 4);
    }
}

static void c2p_write (int offset, uae_u32 v)
{
    if (offset == 3)
        chunky[write_pos] = 0;
    chunky[write_pos] |= (v & 0xff) << (8 * (3 - offset));
    if (offset == 0)
        write_pos = (write_pos + 1) & 7;
    read_pos = 0;
}

static uae_u32 c2p_read (int offset)
{
    uae_u32 v;

    if (read_pos == 0 && offset == 3)
        c2p_convert ();
    write_pos = 0;
    v = planar[read_pos];
    if (offset == 0)
        read_pos = (read_pos + 1) & 7;
    return (v >> (8 * (3 - offset))) & 0xff;
}

int akiko_c2p_range (uaecptr addr)
{
    return currprefs.akiko_c2p && addr >= AKIKO_C2P_BASE && addr < AKIKO_C2P_BASE + 0x100;
}

uae_u32 akiko_c2p_bget (uaecptr addr)
{
    addr &= 0xff;
    /* the ID long $C0CACAFE (akiko.c gave only the $CAFE half) */
    if (addr == 0x00)
        return 0xc0;
    if (addr == 0x01 || addr == 0x02)
        return 0xca;
    if (addr == 0x03)
        return 0xfe;
    if (addr >= 0x38 && addr <= 0x3b)
        return c2p_read (addr - 0x38);
    return 0;
}

uae_u32 akiko_c2p_wget (uaecptr addr)
{
    return (akiko_c2p_bget (addr) << 8) | akiko_c2p_bget (addr + 1);
}

uae_u32 akiko_c2p_lget (uaecptr addr)
{
    uae_u32 v;
    /* byte 3 first, as the original: it is what converts */
    v  = akiko_c2p_bget (addr + 3);
    v |= akiko_c2p_bget (addr + 2) << 8;
    v |= akiko_c2p_bget (addr + 1) << 16;
    v |= akiko_c2p_bget (addr + 0) << 24;
    return v;
}

void akiko_c2p_bput (uaecptr addr, uae_u32 v)
{
    addr &= 0xff;
    if (addr >= 0x38 && addr <= 0x3b)
        c2p_write (addr - 0x38, v);
}

void akiko_c2p_wput (uaecptr addr, uae_u32 v)
{
    akiko_c2p_bput (addr + 1, v & 0xff);
    akiko_c2p_bput (addr + 0, (v >> 8) & 0xff);
}

void akiko_c2p_lput (uaecptr addr, uae_u32 v)
{
    akiko_c2p_bput (addr + 3, v & 0xff);
    akiko_c2p_bput (addr + 2, (v >> 8) & 0xff);
    akiko_c2p_bput (addr + 1, (v >> 16) & 0xff);
    akiko_c2p_bput (addr + 0, (v >> 24) & 0xff);
}

void akiko_c2p_reset (void)
{
    write_pos = read_pos = 0;
}
