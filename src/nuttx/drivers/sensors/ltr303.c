/****************************************************************************
 * drivers/sensors/ltr303.c
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

/* 基于 SiFli-SDK customer/peripherals/sensor/LTR303/LTR303.c 寄存器逻辑
 * 移植为 NuttX 字符设备驱动。芯片为 LiteOn LTR-303ALS-01 环境光传感器，
 * I2C 地址 0x29，双通道 16 位光数据（CH0=可见光+红外全光谱，CH1=红外）。
 * 注意：SDK 原实现 SetIntegrationTime / SetMeasurementRate 误写 ALS_CTRL，
 * 本移植修正为写 MEAS_RATE 寄存器。
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/sensors/ltr303.h>

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_LTR303)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_LTR303_I2C_FREQUENCY
#  define CONFIG_LTR303_I2C_FREQUENCY 400000
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ltr303_dev_s
{
  FAR struct i2c_master_s *i2c;   /* I2C 接口 */
  uint8_t addr;                   /* I2C 地址 */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     ltr303_readreg(FAR struct ltr303_dev_s *priv,
                              uint8_t regaddr, FAR uint8_t *buf,
                              uint8_t len);
static int     ltr303_writereg(FAR struct ltr303_dev_s *priv,
                               uint8_t regaddr, uint8_t regval);
static int     ltr303_modifyreg(FAR struct ltr303_dev_s *priv,
                                uint8_t regaddr, uint8_t clearbits,
                                uint8_t setbits);
static int     ltr303_config(FAR struct ltr303_dev_s *priv);
static int     ltr303_read_data(FAR struct ltr303_dev_s *priv,
                                FAR struct ltr303_data_s *data);

/* 字符设备方法 */

static ssize_t ltr303_read(FAR struct file *filep, FAR char *buffer,
                           size_t buflen);
static int     ltr303_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_ltr303_fops =
{
  NULL,               /* open */
  NULL,               /* close */
  ltr303_read,        /* read */
  NULL,               /* write */
  NULL,               /* seek */
  ltr303_ioctl,       /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ltr303_readreg
 *
 * Description:
 *   从指定寄存器起连续读取 len 个字节。
 *
 ****************************************************************************/

static int ltr303_readreg(FAR struct ltr303_dev_s *priv,
                          uint8_t regaddr, FAR uint8_t *buf, uint8_t len)
{
  struct i2c_config_s config;
  int ret;

  DEBUGASSERT(priv != NULL);
  DEBUGASSERT(buf != NULL);

  config.frequency = CONFIG_LTR303_I2C_FREQUENCY;
  config.address   = priv->addr;
  config.addrlen   = 7;

  ret = i2c_write(priv->i2c, &config, &regaddr, sizeof(regaddr));
  if (ret < 0)
    {
      snerr("ERROR: ltr303 i2c_write failed: %d\n", ret);
      return ret;
    }

  ret = i2c_read(priv->i2c, &config, buf, len);
  if (ret < 0)
    {
      snerr("ERROR: ltr303 i2c_read failed: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: ltr303_writereg
 *
 * Description:
 *   向寄存器写入单字节。
 *
 ****************************************************************************/

static int ltr303_writereg(FAR struct ltr303_dev_s *priv,
                           uint8_t regaddr, uint8_t regval)
{
  struct i2c_config_s config;
  uint8_t buffer[2];
  int ret;

  DEBUGASSERT(priv != NULL);

  buffer[0] = regaddr;
  buffer[1] = regval;

  config.frequency = CONFIG_LTR303_I2C_FREQUENCY;
  config.address   = priv->addr;
  config.addrlen   = 7;

  ret = i2c_write(priv->i2c, &config, buffer, sizeof(buffer));
  if (ret < 0)
    {
      snerr("ERROR: ltr303 i2c_write(reg) failed: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: ltr303_modifyreg
 *
 * Description:
 *   读-改-写寄存器（清除 clearbits，置位 setbits）。
 *
 ****************************************************************************/

static int ltr303_modifyreg(FAR struct ltr303_dev_s *priv,
                            uint8_t regaddr, uint8_t clearbits,
                            uint8_t setbits)
{
  uint8_t regval;
  int ret;

  ret = ltr303_readreg(priv, regaddr, &regval, 1);
  if (ret < 0)
    {
      return ret;
    }

  regval &= ~clearbits;
  regval |= setbits;

  return ltr303_writereg(priv, regaddr, regval);
}

/****************************************************************************
 * Name: ltr303_config
 *
 * Description:
 *   上电配置：校验 ID（软校验，仅告警）→ 使能 + 增益 1x +
 *   积分时间 100ms + 速率 50ms。
 *
 ****************************************************************************/

static int ltr303_config(FAR struct ltr303_dev_s *priv)
{
  uint8_t part_id = 0;
  uint8_t manu_id = 0;
  int ret;

  /* 读取部件/制造商 ID（软校验：不匹配仅告警，便于首版上板观察真实值） */

  ret = ltr303_readreg(priv, LTR303_PART_ID_REG, &part_id, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = ltr303_readreg(priv, LTR303_MANU_ID_REG, &manu_id, 1);
  if (ret < 0)
    {
      return ret;
    }

  sninfo("LTR303 part id = 0x%02x (expect 0x%02x), manu id = 0x%02x\n",
         part_id, LTR303_PART_ID_VALUE, manu_id);

  if (part_id != LTR303_PART_ID_VALUE)
    {
      snwarn("WARN: LTR303 unexpected part id 0x%02x\n", part_id);
    }

  /* 使能 ALS + 增益 1x（增益位清零，mode 位置 1） */

  ret = ltr303_writereg(priv, LTR303_ALS_CTRL, LTR303_ALS_CTRL_MODE);
  if (ret < 0)
    {
      return ret;
    }

  /* 积分时间 100ms（bits[5:3]=0）+ 测量速率 50ms（bits[2:0]=0） */

  return ltr303_writereg(priv, LTR303_MEAS_RATE, 0x00);
}

/****************************************************************************
 * Name: ltr303_read_data
 *
 * Description:
 *   连读 4 字节：CH1 低/高 + CH0 低/高。CH0=全光谱(可见光+红外)，CH1=红外。
 *
 ****************************************************************************/

static int ltr303_read_data(FAR struct ltr303_dev_s *priv,
                            FAR struct ltr303_data_s *data)
{
  uint8_t buf[4];
  int ret;

  DEBUGASSERT(priv != NULL);
  DEBUGASSERT(data != NULL);

  ret = ltr303_readreg(priv, LTR303_CH1DATA, buf, 4);
  if (ret < 0)
    {
      return ret;
    }

  data->ch1 = ((uint16_t)buf[1] << 8) | buf[0];   /* CH1 = 红外(IR) */
  data->ch0 = ((uint16_t)buf[3] << 8) | buf[2];   /* CH0 = 可见光+红外(全光谱) */

  return OK;
}

/****************************************************************************
 * Name: ltr303_read
 *
 * Description:
 *   字符设备 read()：现场读取一次双通道光数据并返回结构体。
 *
 ****************************************************************************/

static ssize_t ltr303_read(FAR struct file *filep, FAR char *buffer,
                           size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ltr303_dev_s *priv = inode->i_private;
  struct ltr303_data_s data;
  int ret;

  DEBUGASSERT(priv != NULL);

  if (buflen < sizeof(data))
    {
      return -EINVAL;
    }

  ret = ltr303_read_data(priv, &data);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(buffer, &data, sizeof(data));
  return sizeof(data);
}

/****************************************************************************
 * Name: ltr303_ioctl
 ****************************************************************************/

static int ltr303_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ltr303_dev_s *priv = inode->i_private;
  int ret;

  DEBUGASSERT(priv != NULL);

  switch (cmd)
    {
    case SNIOC_LTR303READ:
      ret = ltr303_read_data(priv, (FAR struct ltr303_data_s *)arg);
      break;

    default:
      snerr("ERROR: Unrecognized cmd: %d arg: %lu\n", cmd, arg);
      ret = -ENOTTY;
      break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ltr303_register
 ****************************************************************************/

int ltr303_register(FAR const char *devpath,
                    FAR struct i2c_master_s *i2c, uint8_t addr)
{
  FAR struct ltr303_dev_s *priv;
  int ret;

  DEBUGASSERT(devpath != NULL);
  DEBUGASSERT(i2c != NULL);

  priv = kmm_malloc(sizeof(*priv));
  if (priv == NULL)
    {
      snerr("ERROR: Failed to allocate ltr303 instance\n");
      return -ENOMEM;
    }

  priv->i2c  = i2c;
  priv->addr = addr;

  ret = ltr303_config(priv);
  if (ret < 0)
    {
      snerr("ERROR: ltr303_config failed: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  ret = register_driver(devpath, &g_ltr303_fops, 0666, priv);
  if (ret < 0)
    {
      snerr("ERROR: register_driver failed: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  sninfo("LTR303 registered as %s\n", devpath);
  return OK;
}

#endif /* CONFIG_I2C && CONFIG_SENSORS_LTR303 */
