/*
 * drivers/media/platform/s5p-mfc/s5p_mfc_opr_v6.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_OPR_V6_H_
#define S5P_MFC_OPR_V6_H_

#include "s5p_mfc_common.h"
#include "s5p_mfc_opr.h"

#define MFC_CTRL_MODE_CUSTOM	MFC_CTRL_MODE_SFR

#define MB_WIDTH(x_size)		DIV_ROUND_UP(x_size, 16)
#define MB_HEIGHT(y_size)		DIV_ROUND_UP(y_size, 16)
#define S5P_MFC_DEC_MV_SIZE_V6(x, y)	(MB_WIDTH(x) * \
					(((MB_HEIGHT(y)+1)/2)*2) * 64 + 128)

/* Definition */
#define ENC_MULTI_SLICE_MB_MAX		((1 << 30) - 1)
#define ENC_MULTI_SLICE_BIT_MIN		2800
#define ENC_INTRA_REFRESH_MB_MAX	((1 << 18) - 1)
#define ENC_VBV_BUF_SIZE_MAX		((1 << 30) - 1)
#define ENC_H264_LOOP_FILTER_AB_MIN	-12
#define ENC_H264_LOOP_FILTER_AB_MAX	12
#define ENC_H264_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H263_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H264_PROFILE_MAX		3
#define ENC_H264_LEVEL_MAX		42
#define ENC_MPEG4_VOP_TIME_RES_MAX	((1 << 16) - 1)
#define FRAME_DELTA_H264_H263		1
#define TIGHT_CBR_MAX			10

/* Definitions for shared memory compatibility */
#define PIC_TIME_TOP_V6		S5P_FIMV_D_RET_PICTURE_TAG_TOP_V6
#define PIC_TIME_BOT_V6		S5P_FIMV_D_RET_PICTURE_TAG_BOT_V6
#define CROP_INFO_H_V6		S5P_FIMV_D_DISPLAY_CROP_INFO1_V6
#define CROP_INFO_V_V6		S5P_FIMV_D_DISPLAY_CROP_INFO2_V6

struct s5p_mfc_hw_ops *s5p_mfc_init_hw_ops_v6(void);
#endif /* S5P_MFC_OPR_V6_H_ */
