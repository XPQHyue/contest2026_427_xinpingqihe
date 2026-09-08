/****************************************************************************
 * include/nuttx/sensors/mmc5603.h
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

#ifndef __INCLUDE_NUTTX_SENSORS_MMC5603_H
#define __INCLUDE_NUTTX_SENSORS_MMC5603_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sensors/ioctl.h>

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_MMC5603)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C 地址（MMC5603NJ 固定 7-bit 地址 0x30） */

#define MMC5603_ADDR            0x30

/* 寄存器地址 */

#define MMC5603_OUT_X_L         0x00  /* X 轴低字节，连续读 9 字节得到三轴 20bit 数据 */
#define MMC5603_OUT_TEMP        0x09  /* 温度（1 字节有符号） */
#define MMC5603_STATUS_REG      0x18  /* 状态寄存器 */
#define MMC5603_ODR_REG         0x1A  /* 数据速率（0~255 或 1000Hz 特殊值） */
#define MMC5603_CTRL0_REG       0x1B  /* 控制寄存器 0 */
#define MMC5603_CTRL1_REG       0x1C  /* 控制寄存器 1 */
#define MMC5603_CTRL2_REG       0x1D  /* 控制寄存器 2 */
#define MMC5603_PRODUCT_ID      0x39  /* 产品 ID 寄存器 */

/* 产品 ID 期望值 */

#define MMC5603_CHIP_ID         0x10

/* 控制寄存器位定义（位含义对照 Zephyr 主线驱动 mmc56x3：SET/RESET 为消偏
 * 脉冲；CTRL0 0x80 = 连续测量 CMM_FREQ；CTRL2 0x10 = CMM_EN） */

#define MMC5603_CTRL0_TAKE_M    0x01  /* 触发磁场单次测量（TM_M） */
#define MMC5603_CTRL0_TAKE_T    0x02  /* 触发温度单次测量（TM_T） */
#define MMC5603_CTRL0_CMD_SET   0x08  /* SET 脉冲：磁畴置位，抵消偏置 */
#define MMC5603_CTRL0_CMD_RESET 0x10  /* RESET 脉冲：磁畴复位，抵消偏置 */
#define MMC5603_CTRL0_AUTO_SR   0x20  /* 连续模式下自动 SET/RESET 消偏 */
#define MMC5603_CTRL0_CMM_FREQ  0x80  /* 连续测量模式使能 */
#define MMC5603_CTRL1_SW_RST    0x80  /* 软件复位 */
#define MMC5603_CTRL2_CMM_EN    0x10  /* 连续测量输出使能 */
#define MMC5603_CTRL2_HPOWER    0x40  /* 高功率模式 */
#define MMC5603_CTRL2_ODR_1000  0x80  /* 1000Hz 特殊数据速率位 */

/* 磁场量纲：20 位有符号原始值（已减中心偏移 2^19），
 * 每 LSB 对应 0.0625 mG，量程约 ±30 G。
 */

#define MMC5603_CENTER_OFFSET   (1 << 19)          /* 中心偏移 2^19 = 524288 */
#define MMC5603_MAG_SCALE_MG    0.0625f            /* 1 LSB = 0.0625 mG */

/* 自定义 ioctl：读取三轴磁场（配合 read() 使用） */

#define SNIOC_MMC5603READ       _SNIOC(0x006f)     /* Arg: struct mmc5603_data_s* */

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct i2c_master_s;

/* 磁场数据容器（20 位有符号原始值，已减中心偏移） */

struct mmc5603_data_s
{
  int32_t x;            /* X 轴磁场，单位：counts（×0.0625 得 mG） */
  int32_t y;            /* Y 轴磁场 */
  int32_t z;            /* Z 轴磁场 */
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
 * Name: mmc5603_register
 *
 * Description:
 *   注册 MMC5603 地磁传感器字符设备为 'devpath'（如 "/dev/mag0"）。
 *   注册时会完成软件复位、连续测量模式与数据速率配置。
 *
 * Input Parameters:
 *   devpath - 设备路径，例如 "/dev/mag0"
 *   i2c     - 已初始化的 I2C 主设备实例
 *   addr    - I2C 地址（应为 MMC5603_ADDR）
 *
 * Returned Value:
 *   成功返回 OK(0)，失败返回负的 errno。
 *
 ****************************************************************************/

int mmc5603_register(FAR const char *devpath,
                     FAR struct i2c_master_s *i2c, uint8_t addr);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_I2C && CONFIG_SENSORS_MMC5603 */
#endif /* __INCLUDE_NUTTX_SENSORS_MMC5603_H */
