/* Future Retro Fusion */
/*
 * The 68030's MMU, address translation only (mmu030.c): newcpu.c's MMU
 * instructions hand it the registers whenever they change.
 */

/* the tables walked again and put into the bank table (TC off: undone) */
extern void mmu030_update (uae_u32 tc, const uae_u32 *crp, const uae_u32 *srp, const uae_u32 *tt);
/* memory_reset maps the banks anew: nothing of ours to undo */
extern void mmu030_forget (void);
/* PTEST: MMUSR for one address, *descaddr the last descriptor's address */
extern uae_u16 mmu030_ptest (uae_u32 tc, const uae_u32 *crp, const uae_u32 *srp, const uae_u32 *tt,
                             uaecptr a, uae_u32 *descaddr);
