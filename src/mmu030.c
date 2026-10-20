/* Future Retro Fusion */
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * The 68030's MMU, address translation only (prefs cpu_030): what ROM
  * remappers (BlizKick, MuFastROM, CPU FASTROM) and plain identity maps
  * need. No bus errors: an invalid page reads 0, a write-protected one
  * drops writes (as ROM does); Enforcer/MuForce/VMM need more than this.
  *
  * Instead of translating every access, the tables are walked when the
  * guest changes them (PMOVE to TC/CRP/SRP/TT0/TT1, PFLUSH, PLOAD - a 68030
  * program has to flush after changing descriptors anyway) and the result
  * put into the 64K bank table: a bank that maps to itself keeps its own
  * handlers and direct RAM access, so an identity map costs nothing; one
  * that maps elsewhere gets mmu_bank, which adds the offset and calls the
  * physical memory's handlers (and keeps direct access when that is RAM);
  * a bank of mixed pages looks each page up.
  *
  * Supervisor and user space are one (CRP's tables; SRP only if SRE and
  * no CRP), FCL tables are looked up for supervisor data.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "mmu030.h"

#define FL_VALID 1
#define FL_WP    2

struct chunk {
    uae_u32 delta;              /* physical = logical + delta */
    uae_u8 fl;                  /* FL_*; pages: per page instead */
    uae_u8 used;                /* the walk set it */
    uae_u8 installed;           /* mmu_bank is in mem_banks for it */
    uae_u32 *pdelta;            /* mixed: per page */
    uae_u8 *pfl;
};

static struct chunk *chunks;
static addrbank **phys_banks;   /* the bank table as it is without the MMU */
static uae_u8 **phys_rd, **phys_wd;
static addrbank *orig_bank[MEMORY_BANKS];
static uae_u32 orig_off[MEMORY_BANKS];

static int ps, is, ti[4], nlev;
static int warned;

/* the physical memory: the banks without the MMU's */
#define PHYS(a) (phys_banks[(uae_u32) (a) >> 16])

static uae_u32 phys_lget (uae_u32 a)
{
    return PHYS (a)->lget (a);
}

/* logical a to physical *p; 0: invalid, or a write to a write-protected page */
STATIC_INLINE int xl (uaecptr a, uae_u32 *p, int write)
{
    struct chunk *c = &chunks[a >> 16];
    uae_u32 delta = c->delta;
    int fl = c->fl;

    if (c->pdelta) {
        int pg = (a & 0xffff) >> ps;
        delta = c->pdelta[pg];
        fl = c->pfl[pg];
    }
    if (!(fl & FL_VALID) || (write && (fl & FL_WP))) {
        if (warned < 10) {
            warned++;
            write_log ("68030 MMU: %s %08x %s (no bus error emulated)\n", write ? "write to" : "read of",
                       a, fl & FL_VALID ? "write-protected, dropped" : "invalid, reads 0");
        }
        return 0;
    }
    *p = a + delta;
    return 1;
}

static uae_u32 REGPARAM2 mmu_lget (uaecptr a)
{
    uae_u32 p;
    return xl (a, &p, 0) ? PHYS (p)->lget (p) : 0;
}
static uae_u32 REGPARAM2 mmu_wget (uaecptr a)
{
    uae_u32 p;
    return xl (a, &p, 0) ? PHYS (p)->wget (p) : 0;
}
static uae_u32 REGPARAM2 mmu_bget (uaecptr a)
{
    uae_u32 p;
    return xl (a, &p, 0) ? PHYS (p)->bget (p) : 0;
}
static void REGPARAM2 mmu_lput (uaecptr a, uae_u32 v)
{
    uae_u32 p;
    if (xl (a, &p, 1))
        PHYS (p)->lput (p, v);
}
static void REGPARAM2 mmu_wput (uaecptr a, uae_u32 v)
{
    uae_u32 p;
    if (xl (a, &p, 1))
        PHYS (p)->wput (p, v);
}
static void REGPARAM2 mmu_bput (uaecptr a, uae_u32 v)
{
    uae_u32 p;
    if (xl (a, &p, 1))
        PHYS (p)->bput (p, v);
}
static int REGPARAM2 mmu_check (uaecptr a, uae_u32 size)
{
    uae_u32 p;
    return xl (a, &p, 0) && PHYS (p)->check (p, size);
}
static uae_u8 *REGPARAM2 mmu_xlate (uaecptr a)
{
    uae_u32 p;
    if (!xl (a, &p, 0))
        p = 0;
    return PHYS (p)->xlateaddr (p);
}

static addrbank mmu_bank = {
    mmu_lget, mmu_wget, mmu_bget,
    mmu_lput, mmu_wput, mmu_bput,
    mmu_xlate, mmu_check, NULL
};

/* the walk: a logical range [la, la + len) to physical pa */
static void fill (uae_u64 la, uae_u64 len, uae_u32 pa, int fl)
{
    while (len > 0) {
        struct chunk *c = &chunks[(uae_u32) (la >> 16) & 0xffff];
        uae_u32 off = (uae_u32) la & 0xffff;
        uae_u32 n = 0x10000 - off;
        if (n > len)
            n = (uae_u32) len;
        if (n == 0x10000) {
            free (c->pdelta);
            c->pdelta = 0;
            free (c->pfl);
            c->pfl = 0;
            c->delta = pa - (uae_u32) la;
            c->fl = fl;
        } else {                        /* part of the bank: page by page */
            int pages = 0x10000 >> ps, pg;
            if (!c->pdelta) {
                c->pdelta = malloc (pages * sizeof *c->pdelta);
                c->pfl = calloc (pages, 1);
                if (!c->pdelta || !c->pfl)
                    return;
                for (pg = 0; pg < pages; pg++)
                    c->pdelta[pg] = c->delta, c->pfl[pg] = c->used ? c->fl : 0;
            }
            for (pg = off >> ps; pg < (int) ((off + n) >> ps); pg++)
                c->pdelta[pg] = pa - (uae_u32) la, c->pfl[pg] = fl;
        }
        c->used = 1;
        la += n;
        pa += n;
        len -= n;
    }
}

/* with the initial shift's address bits, which the MMU ignores: every alias */
static void emit (uae_u32 la, uae_u64 len, uae_u32 pa, int fl)
{
    uae_u64 k;
    if (!is) {
        fill (la, len, pa, fl);
        return;
    }
    for (k = 0; k < ((uae_u64) 1 << is); k++)
        fill ((k << (32 - is)) + la, len, pa, fl);
}

/* a descriptor: dt, the address it holds, write protection, limit */
struct desc {
    int dt, wp, lu;
    uae_u32 addr, limit;
};

static void read_desc (uae_u32 a, int longfmt, struct desc *d)
{
    uae_u32 d0 = phys_lget (a);

    d->dt = d0 & 3;
    d->wp = (d0 >> 2) & 1;
    if (longfmt) {
        d->lu = d0 >> 31;
        d->limit = (d0 >> 16) & 0x7fff;
        d->addr = phys_lget (a + 4);
    } else {
        d->lu = 0;
        d->limit = 0x7fff;
        d->addr = d0;
    }
}

static int shift_after (int lev)
{
    int i, s = 32 - is;
    for (i = 0; i <= lev; i++)
        s -= ti[i];
    return s;
}

/* a table at level lev (entries long or short), covering logical lbase on */
static void walk (int lev, uae_u32 table, int longfmt, uae_u32 lbase, int lu, uae_u32 limit, int wp)
{
    int n = ti[lev], sh = shift_after (lev);
    uae_u32 i;

    for (i = 0; i < (1u << n); i++) {
        uae_u32 la = lbase + (i << sh);
        uae_u64 span = (uae_u64) 1 << sh;
        struct desc d;

        if (lu ? i < limit : i > limit) {
            emit (la, span, 0, 0);
            continue;
        }
        read_desc (table + i * (longfmt ? 8 : 4), longfmt, &d);
        if (d.dt == 0) {
            emit (la, span, 0, 0);
        } else if (d.dt == 1) {         /* a page, maybe early termination */
            emit (la, span, d.addr & ~0xff, FL_VALID | ((wp | d.wp) ? FL_WP : 0));
        } else if (lev + 1 < nlev) {
            walk (lev + 1, d.addr & ~0xf, d.dt == 3, la, d.lu, d.limit, wp | d.wp);
        } else {                        /* the last level: an indirect descriptor */
            struct desc pd;
            read_desc (d.addr & ~3, d.dt == 3, &pd);
            if (pd.dt == 1)
                emit (la, span, pd.addr & ~0xff, FL_VALID | ((wp | d.wp | pd.wp) ? FL_WP : 0));
            else
                emit (la, span, 0, 0);
        }
    }
}

/* TC's fields; 0: not a configuration the MMU takes */
static int config (uae_u32 tc)
{
    int i, sum;

    ps = (tc >> 20) & 15;
    is = (tc >> 16) & 15;
    for (i = 0; i < 4; i++)
        ti[i] = (tc >> (12 - 4 * i)) & 15;
    for (nlev = 0; nlev < 4 && ti[nlev]; nlev++)
        ;
    for (sum = is + ps, i = 0; i < nlev; i++)
        sum += ti[i];
    return ps >= 8 && nlev > 0 && sum == 32;
}

/* the root to use: CRP, SRP if SRE and CRP invalid; FCL: supervisor data */
static void root (uae_u32 tc, const uae_u32 *crp, const uae_u32 *srp, struct desc *r)
{
    const uae_u32 *rp = ((tc >> 25) & 1) && (crp[0] & 3) == 0 ? srp : crp;

    r->dt = rp[0] & 3;
    r->wp = 0;
    r->lu = rp[0] >> 31;
    r->limit = (rp[0] >> 16) & 0x7fff;
    r->addr = rp[1];
    if (((tc >> 24) & 1) && (r->dt == 2 || r->dt == 3)) {
        struct desc d;
        read_desc ((r->addr & ~0xf) + 5 * (r->dt == 3 ? 8 : 4), r->dt == 3, &d);
        *r = d;
    }
}

/* transparent translation: the 16 MB blocks TT0/TT1 leave as they are */
static int tt_block (const uae_u32 *tt, uae_u32 block)
{
    int i;
    for (i = 0; i < 2; i++)
        if ((tt[i] & 0x8000) && !((block ^ (tt[i] >> 24)) & ~(tt[i] >> 16) & 0xff))
            return 1;
    return 0;
}

static int alloc_tables (void)
{
    if (chunks)
        return 1;
    chunks = calloc (MEMORY_BANKS, sizeof *chunks);
    phys_banks = calloc (MEMORY_BANKS, sizeof *phys_banks);
    phys_rd = calloc (MEMORY_BANKS, sizeof *phys_rd);
    phys_wd = calloc (MEMORY_BANKS, sizeof *phys_wd);
    if (chunks && phys_banks && phys_rd && phys_wd)
        return 1;
    free (chunks); free (phys_banks); free (phys_rd); free (phys_wd);
    chunks = 0;
    write_log ("68030 MMU: no memory for its tables\n");
    return 0;
}

/* the banks back as they were (those still ours: something else may have
   mapped one since) */
static void restore_banks (void)
{
    unsigned int b;

    if (!chunks)
        return;
    for (b = 0; b < MEMORY_BANKS; b++)
        if (chunks[b].installed) {
            if (mem_banks[b] == &mmu_bank)
                put_mem_bank (b << 16, orig_bank[b], (b << 16) - orig_off[b]);
            chunks[b].installed = 0;
        }
}

void mmu030_forget (void)
{
    unsigned int b;

    if (!chunks)
        return;
    for (b = 0; b < MEMORY_BANKS; b++)
        chunks[b].installed = 0;        /* memory_reset maps the banks anew */
}

void mmu030_update (uae_u32 tc, const uae_u32 *crp, const uae_u32 *srp, const uae_u32 *tt)
{
    unsigned int b;
    int moved = 0, mixed = 0, invalid = 0, wp = 0;
    struct desc r;

    restore_banks ();
    if (!(tc & 0x80000000))
        return;                         /* off: plain physical addresses */
    if (!config (tc)) {
        write_log ("68030 MMU: TC %08x is no valid configuration, MMU left off\n", tc);
        return;
    }
    if (!alloc_tables ())
        return;
    for (b = 0; b < MEMORY_BANKS; b++) {
        phys_banks[b] = mem_banks[b];
        phys_rd[b] = mem_rdirect[b];
        phys_wd[b] = mem_wdirect[b];
        free (chunks[b].pdelta);
        free (chunks[b].pfl);
        memset (&chunks[b], 0, sizeof chunks[b]);
    }

    root (tc, crp, srp, &r);
    if (r.dt == 1)
        emit (0, (uae_u64) 1 << (32 - is), r.addr & ~0xff, FL_VALID);
    else if (r.dt >= 2)
        walk (0, r.addr & ~0xf, r.dt == 3, 0, r.lu, r.limit, 0);

    for (b = 0; b < MEMORY_BANKS; b++) {
        struct chunk *c = &chunks[b];

        if (tt_block (tt, b >> 8)) {    /* transparent: as it is */
            free (c->pdelta); c->pdelta = 0;
            free (c->pfl); c->pfl = 0;
            c->delta = 0;
            c->fl = FL_VALID;
            c->used = 1;
        }
        if (!c->used)
            c->fl = 0;
        if (c->pdelta) {                /* pages all alike after all: one bank */
            int pg, pages = 0x10000 >> ps;
            for (pg = 1; pg < pages && c->pdelta[pg] == c->pdelta[0] && c->pfl[pg] == c->pfl[0]; pg++)
                ;
            if (pg == pages) {
                c->delta = c->pdelta[0];
                c->fl = c->pfl[0];
                free (c->pdelta); c->pdelta = 0;
                free (c->pfl); c->pfl = 0;
            }
        }
        if (!c->pdelta && c->delta == 0 && c->fl == FL_VALID)
            continue;                   /* itself: its own bank stays */

        orig_bank[b] = mem_banks[b];
        orig_off[b] = memory_direct_offset (b);
        put_mem_bank (b << 16, &mmu_bank, b << 16);
        c->installed = 1;
        if (c->pdelta)
            mixed++;
        else if (!(c->fl & FL_VALID))
            invalid++;
        else {
            /* RAM behind it: direct access, host = phys p + logical + delta */
            uae_u32 pb = (b << 16) + c->delta, pb2 = pb + 0xffff;
            moved++;
            if (c->fl & FL_WP)
                wp++;
            if (phys_rd[pb >> 16] && phys_rd[pb >> 16] == phys_rd[pb2 >> 16])
                mem_rdirect[b] = phys_rd[pb >> 16] + c->delta;
            if (!(c->fl & FL_WP) && phys_wd[pb >> 16] && phys_wd[pb >> 16] == phys_wd[pb2 >> 16])
                mem_wdirect[b] = phys_wd[pb >> 16] + c->delta;
        }
    }
    write_log ("68030 MMU: on, TC %08x - %d banks moved (%d write-protected), %d mixed, %d invalid\n",
               tc, moved, wp, mixed, invalid);
}

/* PTEST: the table search for one address, as MMUSR; *descaddr: the last
   descriptor's address */
uae_u16 mmu030_ptest (uae_u32 tc, const uae_u32 *crp, const uae_u32 *srp, const uae_u32 *tt,
                      uaecptr a, uae_u32 *descaddr)
{
    uae_u16 sr = 0;
    struct desc d;
    int lev, longfmt, lu, wp = 0, levels = 0;
    uae_u32 table, limit, m;

    *descaddr = 0;
    if (tt_block (tt, a >> 24))
        return 0x0040;                  /* T */
    if (!(tc & 0x80000000) || !config (tc))
        return 0x0400;                  /* I */
    if (!phys_banks || !phys_banks[0]) {
        unsigned int b;
        if (!alloc_tables ())
            return 0x0400;
        for (b = 0; b < MEMORY_BANKS; b++)
            phys_banks[b] = mem_banks[b];
    }
    root (tc, crp, srp, &d);
    if (d.dt == 0)
        return 0x0400;
    table = d.addr & ~0xf;
    longfmt = d.dt == 3;
    lu = d.lu;
    limit = d.limit;
    for (lev = 0; lev < nlev && d.dt >= 2; lev++) {
        uae_u32 i;
        int s;
        for (s = 0, m = 0; m < (uae_u32) lev; m++)
            s += ti[m];
        /* this level's index bits: below the initial shift and the levels above */
        i = (uae_u32) (((uae_u64) a << (is + s)) & 0xffffffffu) >> (32 - ti[lev]);
        if (lu ? i < limit : i > limit)
            return sr | 0x4400 | levels;  /* L, I */
        *descaddr = table + i * (longfmt ? 8 : 4);
        read_desc (*descaddr, longfmt, &d);
        levels++;
        wp |= d.wp;
        if (d.dt >= 2 && lev + 1 == nlev) {     /* indirect */
            *descaddr = d.addr & ~3;
            read_desc (*descaddr, d.dt == 3, &d);
            levels++;
            wp |= d.wp;
            if (d.dt != 1)
                return 0x0400 | levels;
        }
        table = d.addr & ~0xf;
        longfmt = d.dt == 3;
        lu = d.lu;
        limit = d.limit;
    }
    if (d.dt == 0)
        return 0x0400 | levels;
    if (wp)
        sr |= 0x0800;                   /* W */
    if (*descaddr && (phys_lget (*descaddr) & 0x10))
        sr |= 0x0200;                   /* M: the page descriptor's */
    return sr | (levels & 7);
}
