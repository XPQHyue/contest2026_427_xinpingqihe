/****************************************************************************
 * apps/examples/phywear/pw_power.h
 *
 * [AGENT-WIN11] 电源/电量接口：VBAT 采样、充电检测、背光（息屏/亮屏）。
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_POWER_H
#define __APPS_EXAMPLES_PHYWEAR_PW_POWER_H

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_PHYWEAR_POWER

/* 采一次电池电压（mV）；失败返回负 errno */

int pw_power_vbat_mv(void);

/* 电量百分比 0..100（3.40 V~4.20 V 线性），失败返回负 errno */

int pw_power_pct(void);

/* 是否在充电（VBUS_DET）；1=在充 0=未充 <0=未知 */

int pw_power_charging(void);

/* 缓存状态（5 s 内复用上次采样）：pct 0..100、chg 1/0；返回 0 有效，
 * -EAGAIN 表示还没有成功采到电压。UI 顶栏用这个，别每帧去开 /dev/adc0。 */

int pw_power_state(int *pct, int *chg);

/* 最近一次 ADC 引脚原始 mV（未乘分压），标定时对照万用表用 */

int pw_power_pin_mv(void);

/* 现场标定：用万用表量到电池真实电压 ref_mv，结合此刻的 ADC 引脚值反推
 * 换算系数并立即生效（掉电失效，但会把该写进代码的常数打印出来）。
 * 返回 0 成功，负 errno 失败。 */

int pw_power_set_ref(int ref_mv);

/* 采样环形缓冲：每 5 s 记一条，最多 64 条（约 5 分钟），掉电即失。
 * 用途：拔掉 USB 前后对比"引脚读数是否真跟着电池走" —— 串口一拔就断，
 * 只有板子自己记得下来。 */

void pw_power_log_dump(void);

/* 背光 0..100（0 = 熄屏） */

int pw_power_backlight(int level);

/* 息屏 / 亮屏（亮屏回到 PW_POWER_BL_ON） */

int pw_power_screen_off(void);
int pw_power_screen_on(void);

/* 启动时打一条快照日志，便于串口核对 */

void pw_power_report(void);

#endif
#endif /* __APPS_EXAMPLES_PHYWEAR_PW_POWER_H */
