/****************************************************************************
 * drivers/sensors/mmc5603.c
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

/* 基于 SiFli-SDK customer/peripherals/sensor/MMC56x3/mmc56x3.c 寄存器逻辑
 * 移植为 NuttX 字符设备驱动。芯片为美新 MMC5603NJ 三轴地磁传感器，
 * I2C 地址 0x30，输出 20 位磁场数据（0.0625 mG/LSB，量程 ±30 G）。
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
#include <nuttx/signal.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/sensors/mmc5603.h>

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_MMC5603)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_MMC5603_I2C_FREQUENCY
#  define CONFIG_MMC5603_I2C_FREQUENCY 400000
#endif

#define MMC5603_DATA_LEN        9   /* X[15:0] Y[15:0] Z[15:0] + 3 个高 4bit */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mmc5603_dev_s
{
  FAR struct i2c_master_s *i2c;   /* I2C 接口 */
  uint8_t addr;                   /* I2C 地址 */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     mmc5603_readreg(FAR struct mmc5603_dev_s *priv,
                               uint8_t regaddr, FAR uint8_t *buf,
                               uint8_t len);
static int     mmc5603_writereg(FAR struct mmc5603_dev_s *priv,
                                uint8_t regaddr, uint8_t regval);
static int     mmc5603_read_id(FAR struct mmc5603_dev_s *priv,
                               FAR uint8_t *id);
static int     mmc5603_reset(FAR struct mmc5603_dev_s *priv);
static int     mmc5603_set_continuous(FAR struct mmc5603_dev_s *priv,
                                      bool continuous);
static int     mmc5603_set_datarate(FAR struct mmc5603_dev_s *priv,
                                    uint16_t rate);
static int     mmc5603_config(FAR struct mmc5603_dev_s *priv);
static int     mmc5603_read_data(FAR struct mmc5603_dev_s *priv,
                                 FAR struct mmc5603_data_s *data);

/* 字符设备方法 */

static ssize_t mmc5603_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen);
static int     mmc5603_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_mmc5603_fops =
{
  NULL,               /* open */
  NULL,               /* close */
  mmc5603_read,       /* read */
  NULL,               /* write */
  NULL,               /* seek */
  mmc5603_ioctl,      /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mmc5603_readreg
 *
 * Description:
 *   从指定寄存器起连续读取 len 个字节（写寄存器地址后 RESTART 读）。
 *
 ****************************************************************************/

static int mmc5603_readreg(FAR struct mmc5603_dev_s *priv,
                           uint8_t regaddr, FAR uint8_t *buf, uint8_t len)
{
  struct i2c_config_s config;
  int ret;

  DEBUGASSERT(priv != NULL);
  DEBUGASSERT(buf != NULL);

  config.frequency = CONFIG_MMC5603_I2C_FREQUENCY;
  config.address   = priv->addr;
  config.addrlen   = 7;

  /* 先写寄存器地址 */

  ret = i2c_write(priv->i2c, &config, &regaddr, sizeof(regaddr));
  if (ret < 0)
    {
      snerr("ERROR: mmc5603 i2c_write failed: %d\n", ret);
      return ret;
    }

  /* RESTART 后连续读取 len 字节 */

  ret = i2c_read(priv->i2c, &config, buf, len);
  if (ret < 0)
    {
      snerr("ERROR: mmc5603 i2c_read failed: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: mmc5603_writereg
 *
 * Description:
 *   向寄存器写入单字节（寄存器地址 + 数据，无 RESTART）。
 *
 ****************************************************************************/

static int mmc5603_writereg(FAR struct mmc5603_dev_s *priv,
                            uint8_t regaddr, uint8_t regval)
{
  struct i2c_config_s config;
  uint8_t buffer[2];
  int ret;

  DEBUGASSERT(priv != NULL);

  buffer[0] = regaddr;
  buffer[1] = regval;

  config.frequency = CONFIG_MMC5603_I2C_FREQUENCY;
  config.address   = priv->addr;
  config.addrlen   = 7;

  ret = i2c_write(priv->i2c, &config, buffer, sizeof(buffer));
  if (ret < 0)
    {
      snerr("ERROR: mmc5603 i2c_write(reg) failed: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: mmc5603_read_id
 ****************************************************************************/

static int mmc5603_read_id(FAR struct mmc5603_dev_s *priv, FAR uint8_t *id)
{
  return mmc5603_readreg(priv, MMC5603_PRODUCT_ID, id, 1);
}

/****************************************************************************
 * Name: mmc5603_reset
 *
 * Description:
 *   按 SDK 时序执行软件复位：CTRL1=0x80 复位 → 20ms →
 *   CTRL0=0x08(TM_M) → 1ms → CTRL0=0x10(TM_T) → 1ms。
 *
 ****************************************************************************/

static int mmc5603_reset(FAR struct mmc5603_dev_s *priv)
{
  int ret;

  ret = mmc5603_writereg(priv, MMC5603_CTRL1_REG, MMC5603_CTRL1_SW_RST);
  if (ret < 0)
    {
      return ret;
    }

  nxsig_usleep(20000);  /* 20ms 复位等待 */

  ret = mmc5603_writereg(priv, MMC5603_CTRL0_REG, MMC5603_CTRL0_CMD_SET);
  if (ret < 0)
    {
      return ret;
    }

  nxsig_usleep(1000);

  ret = mmc5603_writereg(priv, MMC5603_CTRL0_REG, MMC5603_CTRL0_CMD_RESET);
  if (ret < 0)
    {
      return ret;
    }

  nxsig_usleep(1000);

  return OK;
}

/****************************************************************************
 * Name: mmc5603_set_continuous
 *
 * Description:
 *   置位 CTRL0.SET 后，写 CTRL2 的 CM_FREQ 位切换连续/单次测量模式。
 *
 ****************************************************************************/

static int mmc5603_set_continuous(FAR struct mmc5603_dev_s *priv,
                                  bool continuous)
{
  uint8_t regval;
  int ret;

  ret = mmc5603_writereg(priv, MMC5603_CTRL0_REG,
                              MMC5603_CTRL0_CMM_FREQ | MMC5603_CTRL0_AUTO_SR);
  if (ret < 0)
    {
      return ret;
    }

  ret = mmc5603_readreg(priv, MMC5603_CTRL2_REG, &regval, 1);
  if (ret < 0)
    {
      return ret;
    }

  if (continuous)
    {
      regval |= MMC5603_CTRL2_CMM_EN;
    }
  else
    {
      regval &= ~MMC5603_CTRL2_CMM_EN;
    }

  return mmc5603_writereg(priv, MMC5603_CTRL2_REG, regval);
}

/****************************************************************************
 * Name: mmc5603_set_datarate
 *
 * Description:
 *   设置输出数据速率：rate=1000 用特殊位（ODR=255 + CTRL2.ODR_1000），
 *   否则直接写 ODR 寄存器（0~255）。
 *
 ****************************************************************************/

static int mmc5603_set_datarate(FAR struct mmc5603_dev_s *priv,
                                uint16_t rate)
{
  uint8_t regval;
  int ret;

  if (rate > 255)
    {
      rate = 1000;
    }

  if (rate == 1000)
    {
      ret = mmc5603_writereg(priv, MMC5603_ODR_REG, 255);
      if (ret < 0)
        {
          return ret;
        }

      ret = mmc5603_readreg(priv, MMC5603_CTRL2_REG, &regval, 1);
      if (ret < 0)
        {
          return ret;
        }

      regval |= MMC5603_CTRL2_ODR_1000;
    }
  else
    {
      ret = mmc5603_writereg(priv, MMC5603_ODR_REG, (uint8_t)rate);
      if (ret < 0)
        {
          return ret;
        }

      ret = mmc5603_readreg(priv, MMC5603_CTRL2_REG, &regval, 1);
      if (ret < 0)
        {
          return ret;
        }

      regval &= ~MMC5603_CTRL2_ODR_1000;
    }

  return mmc5603_writereg(priv, MMC5603_CTRL2_REG, regval);
}

/****************************************************************************
 * Name: mmc5603_config
 *
 * Description:
 *   上电配置：校验产品 ID → 复位 → 100Hz 数据速率 → 连续测量模式。
 *
 ****************************************************************************/

static int mmc5603_config(FAR struct mmc5603_dev_s *priv)
{
  uint8_t id = 0;
  int ret;

  /* 读取并校验产品 ID */

  ret = mmc5603_read_id(priv, &id);
  if (ret < 0)
    {
      return ret;
    }

  sninfo("MMC5603 product id = 0x%02x (expect 0x%02x)\n", id,
         MMC5603_CHIP_ID);
  if (id != MMC5603_CHIP_ID)
    {
      snerr("ERROR: MMC5603 wrong product id 0x%02x\n", id);
      return -ENODEV;
    }

  /* 复位 + 配置 */

  ret = mmc5603_reset(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = mmc5603_set_datarate(priv, 100);   /* 100Hz */
  if (ret < 0)
    {
      return ret;
    }

  return mmc5603_set_continuous(priv, true);
}

/****************************************************************************
 * Name: mmc5603_read_data
 *
 * Description:
 *   连续读取 9 字节，解出三轴 20 位有符号磁场值（已减中心偏移 2^19）。
 *   20 位拼接：X = buf[0]<<12 | buf[1]<<4 | buf[6]>>4。
 *
 ****************************************************************************/

static int mmc5603_read_data(FAR struct mmc5603_dev_s *priv,
                             FAR struct mmc5603_data_s *data)
{
  uint8_t buf[MMC5603_DATA_LEN];
  int32_t x;
  int32_t y;
  int32_t z;
  int ret;

  DEBUGASSERT(priv != NULL);
  DEBUGASSERT(data != NULL);

  ret = mmc5603_readreg(priv, MMC5603_OUT_X_L, buf, MMC5603_DATA_LEN);
  if (ret < 0)
    {
      return ret;
    }

  x = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) |
      ((int32_t)buf[6] >> 4);
  y = ((int32_t)buf[2] << 12) | ((int32_t)buf[3] << 4) |
      ((int32_t)buf[7] >> 4);
  z = ((int32_t)buf[4] << 12) | ((int32_t)buf[5] << 4) |
      ((int32_t)buf[8] >> 4);

  /* 减去中心偏移（2^19），得到 20 位有符号值 */

  data->x = x - MMC5603_CENTER_OFFSET;
  data->y = y - MMC5603_CENTER_OFFSET;
  data->z = z - MMC5603_CENTER_OFFSET;

  return OK;
}

/****************************************************************************
 * Name: mmc5603_read
 *
 * Description:
 *   字符设备 read()：现场读取一次三轴磁场并返回结构体。
 *
 ****************************************************************************/

static ssize_t mmc5603_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct mmc5603_dev_s *priv = inode->i_private;
  struct mmc5603_data_s data;
  int ret;

  DEBUGASSERT(priv != NULL);

  if (buflen < sizeof(data))
    {
      return -EINVAL;
    }

  ret = mmc5603_read_data(priv, &data);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(buffer, &data, sizeof(data));
  return sizeof(data);
}

/****************************************************************************
 * Name: mmc5603_ioctl
 ****************************************************************************/

static int mmc5603_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct mmc5603_dev_s *priv = inode->i_private;
  int ret;

  DEBUGASSERT(priv != NULL);

  switch (cmd)
    {
    case SNIOC_MMC5603READ:
      ret = mmc5603_read_data(priv, (FAR struct mmc5603_data_s *)arg);
      break;

    case SNIOC_READID:
      {
        FAR uint8_t *id = (FAR uint8_t *)arg;
        ret = mmc5603_read_id(priv, id);
      }
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
 * Name: mmc5603_register
 ****************************************************************************/

int mmc5603_register(FAR const char *devpath,
                     FAR struct i2c_master_s *i2c, uint8_t addr)
{
  FAR struct mmc5603_dev_s *priv;
  int ret;

  DEBUGASSERT(devpath != NULL);
  DEBUGASSERT(i2c != NULL);

  priv = kmm_malloc(sizeof(*priv));
  if (priv == NULL)
    {
      snerr("ERROR: Failed to allocate mmc5603 instance\n");
      return -ENOMEM;
    }

  priv->i2c  = i2c;
  priv->addr = addr;

  /* 上电配置（含产品 ID 校验，失败则释放内存返回错误） */

  ret = mmc5603_config(priv);
  if (ret < 0)
    {
      snerr("ERROR: mmc5603_config failed: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  ret = register_driver(devpath, &g_mmc5603_fops, 0666, priv);
  if (ret < 0)
    {
      snerr("ERROR: register_driver failed: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  sninfo("MMC5603 registered as %s\n", devpath);
  return OK;
}

#endif /* CONFIG_I2C && CONFIG_SENSORS_MMC5603 */
