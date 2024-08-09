﻿/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2022. All rights reserved.
 * Description: Header file for hal_pm_rom.c.
 */

#ifndef HAL_PM_ROM_H
#define HAL_PM_ROM_H

/*****************************************************************************
  其他头文件包含
*****************************************************************************/
#include "osal_types.h"
#include "hal_common_ops_rom.h"
#include "hal_chip_rom.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*****************************************************************************
  2 宏定义
*****************************************************************************/
#define MIN_SLEEP_TIME                  1000        /* tsf小于该值不睡眠 load时间(500us)+500us裕量 */
#define VAP0_EXT_TSF_EN_OFFSET          BIT1        /* 外置TSF vap0使能位BIT1 */
#define VAP0_EXT_TSF_WORK_STATUS_OFFSET BIT0        /* VAP0 外置TSF工作状态 */
#define EXT_TSF_SLEEP_EN_OFFSET         BIT0        /* 外置TSF使能位 */
#define SEC_TO_NSEC  1000000000  /* 1s为1 000 000 000ns */
#define USEC_TO_NSEC 1000     /* 1000.0ns */
#define CHECK_MAX_TIMES 5000  /* 最大5000次检查 */
#define CHECK_PER_TIME 1000   /* 每次1000us时延 */
#define EXT_OFFSET_ADJ_TIME             5           /* TSF翻转后调整提前量的次数 规避偶现的异常翻转导致增大提前量 */
#define EXT_OFFSET_ADJ_RATIO            200         /* 提前量调整比例 = TIME/RATIO 目前5/200=2.5% */
#define EXT_OFFSET_COMP_RESV            50          /* 外置提前量补偿裕量us */
#define PM_WLAN_TIMEOUT_INIT            0xFFFFFFUL  /* WLAN超时配置 */
typedef struct {
    osal_u16 clk_div5;                /* 0x40000050 MAC主时钟分频系数 */
    osal_u16 ch_sel;                  /* 0x40000140 模拟通道与主辅路链接关系选择 */
    osal_u16 clk_phy_sel;             /* 0x40000144 PHY时钟选择 */
    osal_u16 dtcm_mode;               /* 0x40000280 dtcm冲突选择模式 */
}hal_pm_w_ctl_stru;
typedef struct {
    osal_u32 powerdown_cnt;           /* 睡眠次数,用于判断投票期间芯片是否掉电 */
    osal_u8  state;                   /* 低功耗状态 */
    osal_u8  sta_exist;               /* sta是否存在 */
    osal_u8  tsf_flip;                /* 唤醒后tsf翻转次数 */
    osal_u8  wkup_cnt;                /* 唤醒次数 用于统计翻转比例 */
    osal_u8  enable_sleep;           /* 仅STA & D2D共存下使用 */
}hal_pm_ctrl_stru;

typedef osal_void (*hh503_vap_cb_set_twt)(const hal_to_dmac_vap_stru *hal_vap, const twt_reg_param_stru *twt_param,
    osal_u8 count, osal_u8 trigger);
typedef osal_void (*hal_vap_cb_pm_vote_plat)(hal_pm_mode_enum_uint8 pm_mode, hal_pm_ctrl_stru *pm_ctl);

/*****************************************************************************
  函数声明
*****************************************************************************/
osal_void hal_pm_disable_front_end_tx(hal_to_dmac_device_stru *hal_device);
osal_void hal_pm_enable_front_end(hal_to_dmac_device_stru  *hal_device, osal_u8 enable_paldo);
osal_void hal_unmask_interrupt(osal_u32 offset);
hal_pm_ctrl_stru *hal_get_pm_ctrl(osal_void);
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* end of hal_pm_rom.h */
