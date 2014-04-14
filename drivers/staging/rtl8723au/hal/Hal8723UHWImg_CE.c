/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

/*Created on  2013/01/14, 15:51*/
#include "odm_precomp.h"

u32 Rtl8723UPHY_REG_Array_PG[Rtl8723UPHY_REG_Array_PGLength] = {
	0xe00, 0xffffffff, 0x0a0c0c0c,
	0xe04, 0xffffffff, 0x02040608,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x0a0c0d0e,
	0xe14, 0xffffffff, 0x02040608,
	0xe18, 0xffffffff, 0x0a0c0d0e,
	0xe1c, 0xffffffff, 0x02040608,
	0x830, 0xffffffff, 0x0a0c0c0c,
	0x834, 0xffffffff, 0x02040608,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x0a0c0d0e,
	0x848, 0xffffffff, 0x02040608,
	0x84c, 0xffffffff, 0x0a0c0d0e,
	0x868, 0xffffffff, 0x02040608,
	0xe00, 0xffffffff, 0x00000000,
	0xe04, 0xffffffff, 0x00000000,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x00000000,
	0xe14, 0xffffffff, 0x00000000,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x00000000,
	0x834, 0xffffffff, 0x00000000,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x00000000,
	0x848, 0xffffffff, 0x00000000,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	0xe00, 0xffffffff, 0x04040404,
	0xe04, 0xffffffff, 0x00020204,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x06060606,
	0xe14, 0xffffffff, 0x00020406,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x04040404,
	0x834, 0xffffffff, 0x00020204,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x06060606,
	0x848, 0xffffffff, 0x00020406,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	0xe00, 0xffffffff, 0x00000000,
	0xe04, 0xffffffff, 0x00000000,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x00000000,
	0xe14, 0xffffffff, 0x00000000,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x00000000,
	0x834, 0xffffffff, 0x00000000,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x00000000,
	0x848, 0xffffffff, 0x00000000,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	0xe00, 0xffffffff, 0x00000000,
	0xe04, 0xffffffff, 0x00000000,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x00000000,
	0xe14, 0xffffffff, 0x00000000,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x00000000,
	0x834, 0xffffffff, 0x00000000,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x00000000,
	0x848, 0xffffffff, 0x00000000,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	0xe00, 0xffffffff, 0x04040404,
	0xe04, 0xffffffff, 0x00020204,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x00000000,
	0xe14, 0xffffffff, 0x00000000,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x04040404,
	0x834, 0xffffffff, 0x00020204,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x00000000,
	0x848, 0xffffffff, 0x00000000,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	0xe00, 0xffffffff, 0x00000000,
	0xe04, 0xffffffff, 0x00000000,
	0xe08, 0x0000ff00, 0x00000000,
	0x86c, 0xffffff00, 0x00000000,
	0xe10, 0xffffffff, 0x00000000,
	0xe14, 0xffffffff, 0x00000000,
	0xe18, 0xffffffff, 0x00000000,
	0xe1c, 0xffffffff, 0x00000000,
	0x830, 0xffffffff, 0x00000000,
	0x834, 0xffffffff, 0x00000000,
	0x838, 0xffffff00, 0x00000000,
	0x86c, 0x000000ff, 0x00000000,
	0x83c, 0xffffffff, 0x00000000,
	0x848, 0xffffffff, 0x00000000,
	0x84c, 0xffffffff, 0x00000000,
	0x868, 0xffffffff, 0x00000000,
	};

u32 Rtl8723UMACPHY_Array_PG[Rtl8723UMACPHY_Array_PGLength] = {
	0x0,
};
