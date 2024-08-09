﻿/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2022. All rights reserved.
 * Description: Header file of msg_apf_rom
 */

#ifndef MSG_APF_ROM_H
#define MSG_APF_ROM_H

#include "oal_types_device_rom.h"

#define APF_PROGRAM_MAX_LEN 512

#ifdef _PRE_WLAN_FEATURE_APF
typedef struct {
    oal_bool_enum_uint8 is_enabled;
    oal_bool_enum_uint8 rsv; // 保留，4字节对齐
    osal_u16 program_len;
    osal_u32 install_timestamp;
    osal_u8 program[APF_PROGRAM_MAX_LEN];
} mac_apf_stru;
#endif
#endif