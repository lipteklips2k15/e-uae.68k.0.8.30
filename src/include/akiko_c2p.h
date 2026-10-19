/* Future Retro Fusion */
/* Akiko's C2P converter without the CD32 (akiko_c2p.c) */

#define AKIKO_C2P_BASE 0xb80000

extern int akiko_c2p_range (uaecptr addr);   /* on, and addr in Akiko's space */
extern uae_u32 akiko_c2p_bget (uaecptr addr);
extern uae_u32 akiko_c2p_wget (uaecptr addr);
extern uae_u32 akiko_c2p_lget (uaecptr addr);
extern void akiko_c2p_bput (uaecptr addr, uae_u32 v);
extern void akiko_c2p_wput (uaecptr addr, uae_u32 v);
extern void akiko_c2p_lput (uaecptr addr, uae_u32 v);
extern void akiko_c2p_reset (void);
