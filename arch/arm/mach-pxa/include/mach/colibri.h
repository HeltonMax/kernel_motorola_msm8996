#ifndef _COLIBRI_H_
#define _COLIBRI_H_

#include <net/ax88796.h>
#include <mach/mfp.h>

/*
 * common settings for all modules
 */

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
extern void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin);
#else
static inline void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
extern void colibri_pxa3xx_init_lcd(int bl_pin);
#else
static inline void colibri_pxa3xx_init_lcd(int bl_pin) {}
#endif

#if defined(CONFIG_AX88796)
extern void colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data);
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
extern void colibri_pxa3xx_init_nand(void);
#else
static inline void colibri_pxa3xx_init_nand(void) {}
#endif

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* GPIO definitions for Colibri PXA270 */
#define GPIO114_COLIBRI_PXA270_ETH_IRQ	114

#endif /* _COLIBRI_H_ */

