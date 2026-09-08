/****************************************************************************
 * include/nuttx/sensors/ltr303.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_SENSORS_LTR303_H
#define __INCLUDE_NUTTX_SENSORS_LTR303_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sensors/ioctl.h>

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_LTR303)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C 地址（LTR-303ALS-01 固定 7-bit 地址 0x29） */

#define LTR303_ADDR             0x29

/* 寄存器地址 */

#define LTR303_ALS_CTRL         0x80  /* ALS 控制：bit0 使能，bit[4:2] 增益 */
#define LTR303_MEAS_RATE        0x85  /* 测量速率：bit[2:0] 速率，bit[5:3] 积分时间 */
#define LTR303_PART_ID_REG      0x86  /* 部件 ID（期望 0x86） */
#define LTR303_MANU_ID_REG      0x87  /* 制造商 ID（LiteOn = 0x05） */
#define LTR303_CH1DATA          0x88  /* 数据：连读 4 字节 = CH1 低/高 + CH0 低/高 */
#define LTR303_STATUS           0x8C  /* 状态寄存器 */

/* 寄存器位定义 */

#define LTR303_ALS_CTRL_MODE    0x01  /* ALS 使能位（1 = active） */
#define LTR303_ALS_CTRL_GAIN_SHIFT   2
#define LTR303_ALS_CTRL_GAIN_MASK     (0x07 << LTR303_ALS_CTRL_GAIN_SHIFT)
#define LTR303_MEAS_RATE_RATE_MASK    0x07
#define LTR303_MEAS_RATE_INTEG_SHIFT  3
#define LTR303_MEAS_RATE_INTEG_MASK   (0x07 << LTR303_MEAS_RATE_INTEG_SHIFT)

/* 期望的部件/制造商 ID */

#define LTR303_PART_ID_VALUE    0x86
#define LTR303_MANU_ID_VALUE    0x05

/* 自定义 ioctl：读取双通道光数据（配合 read() 使用） */

#define SNIOC_LTR303READ        _SNIOC(0x0070)     /* Arg: struct ltr303_data_s* */

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct i2c_master_s;

/* 光数据容器（原始 ADC 计数值，16 位） */

struct ltr303_data_s
{
  uint16_t ch0;         /* 通道 0（CH0）：可见光 + 红外（全光谱） */
  uint16_t ch1;         /* 通道 1（CH1）：红外 only（IR） */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: ltr303_register
 *
 * Description:
 *   注册 LTR-303 环境光传感器字符设备为 'devpath'（如 "/dev/light0"）。
 *   注册时会完成使能 + 增益/积分时间/速率配置。
 *
 * Input Parameters:
 *   devpath - 设备路径，例如 "/dev/light0"
 *   i2c     - 已初始化的 I2C 主设备实例
 *   addr    - I2C 地址（应为 LTR303_ADDR）
 *
 * Returned Value:
 *   成功返回 OK(0)，失败返回负的 errno。
 *
 ****************************************************************************/

int ltr303_register(FAR const char *devpath,
                    FAR struct i2c_master_s *i2c, uint8_t addr);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_I2C && CONFIG_SENSORS_LTR303 */
#endif /* __INCLUDE_NUTTX_SENSORS_LTR303_H */
