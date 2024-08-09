/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: 温度补偿rf寄存器实现
*/
#ifndef _FE_HAL_RF_REG_IF_TEMPERATE_H_
#define _FE_HAL_RF_REG_IF_TEMPERATE_H_
#include "osal_types.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif
enum {
    TEMP_CODE_COMP_BANK_SEL_0 = 0,  // 温度码补偿使用bank0
    TEMP_CODE_COMP_BANK_SEL_1 = 1,  // 温度码补偿使用bank1
    TEMP_CODE_COMP_BANK_SEL_BUTT
};
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif  // _FE_HAL_RF_REG_IF_TEMPERATE_H_
