﻿/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2022. All rights reserved.
 * Description: Header file of msg_ant_sel_rom
 */

#ifndef MSG_ANT_SEL_ROM_H
#define MSG_ANT_SEL_ROM_H

#include "osal_types.h"

typedef struct {
    osal_u8 cfg_type; /* 配置的场景类型：tx方向七种，rx方向一种 */
    osal_u8 value;    /* 配置的天线分集参数：0-3 */
    osal_u8 reserve[2];
} mac_cfg_ant_sel_param_stru;

#endif