/****************************************************************************
 * apps/examples/phywear/pw_btprobe.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 蓝牙 Phase 0 探针：直接对 `/dev/ttyHCI0` 发 HCI 命令，读控制器自报能力。
 *
 * 为什么自己写而不是用 zblue 的 shell/sample：本树的主机栈是 zblue，但
 * `btsak` 依赖 NuttX 原生 bluetooth 栈（`# CONFIG_BLUETOOTH is not set`），
 * 二者不兼容、不能直接借用。这里只开端口、发 HCI 命令、解析回包，不经过
 * 主机栈的传输层与事件线程，失败点单一、可证伪。
 *
 * 探测序列（每条都等 Command Complete，按 opcode 匹配，跳过别的命令的回包）：
 *   ① HCI_Reset                      0x0C03 —— 链路是否通（Phase A 已验）
 *   ② Read Local Version Information 0x1001 —— HCI/LMP 版本、厂商、子版本
 *   ③ Read BD_ADDR                   0x1009 —— 控制器地址
 *   ④ Read Local Supported Features  0x1003 —— LMP 特性页 0
 *   ⑤ Read Local Supported Commands  0x1002 —— 支持的命令位图
 *
 * 判据（写进 docs/bt-pan-phase0.md）：
 *   - LMP 特性页 0 bit 38（byte4 bit6）= "BR/EDR Not Supported"：
 *       置 1 → 控制器是 LE-only，PAN/BNEP 这条路在控制器层就不成立；
 *       置 0 → 控制器支持 BR/EDR，剩下的是主机栈缺 BNEP。
 *   - bit 37（byte4 bit5）= "LE Supported (Controller)"，用于交叉核对。
 *
 * 注意：本探针会打断正在跑的 zblue 广播（HCI_Reset），只在空闲测试板上用。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pw_btprobe.h"

#define BT_HCI_DEV   "/dev/ttyHCI0"
#define BT_WAIT_MS   2000
#define BT_MAX_EVENTS 32

#define BT_HCI_COMMAND_PKT      0x01
#define BT_HCI_EVT_CMD_COMPLETE 0x0e
#define BT_HCI_EVT_CMD_STATUS   0x0f

#define BT_OP(ogf, ocf)         ((uint16_t)(((ogf) << 10) | (ocf)))
#define BT_OGF_INFO             0x04
#define BT_OCF_READ_LOCAL_VER   0x0001
#define BT_OCF_READ_LOCAL_CMDS  0x0002
#define BT_OCF_READ_LOCAL_FEAT  0x0003
#define BT_OCF_READ_BD_ADDR     0x0009
#define BT_OGF_HOST_CTL         0x03
#define BT_OCF_RESET            0x0003

static void bt_hex(const char *tag, const unsigned char *p, int n)
{
  int i;

  printf("[BTHCI] %s (%d B):", tag, n);
  for (i = 0; i < n; i++)
    {
      printf(" %02x", p[i]);
    }

  printf("\n");
}

/* 发一条无参数命令 */

static int bt_send_cmd(int fd, uint16_t opcode)
{
  unsigned char cmd[4];

  cmd[0] = BT_HCI_COMMAND_PKT;
  cmd[1] = (unsigned char)(opcode & 0xff);
  cmd[2] = (unsigned char)(opcode >> 8);
  cmd[3] = 0x00;

  return write(fd, cmd, sizeof(cmd));
}

/* 等某条命令的 Command Complete，返回 status（0=成功）；<0 = 没等到。
 * params 非 NULL 时把返回参数（不含 status 字节）拷出，*plen 为长度。 */

static int bt_wait_cc(int fd, uint16_t opcode, unsigned char *params,
                      int *plen)
{
  unsigned char buf[256];
  int total = 0;
  int i;

  for (i = 0; i < BT_MAX_EVENTS; i++)
    {
      struct pollfd pfd;
      int n;
      int off = 0;

      pfd.fd = fd;
      pfd.events = POLLIN;

      n = poll(&pfd, 1, BT_WAIT_MS);
      if (n <= 0)
        {
          return -1;
        }

      n = read(fd, buf + total, sizeof(buf) - total);
      if (n <= 0)
        {
          return -2;
        }

      total += n;

      /* 从缓冲区里逐帧找 Command Complete；UART 可能把一帧拆开，所以
       * 帧不完整时保留残包，等下一次 read。 */

      while (total - off >= 3)
        {
          int evt_len;
          int frame;
          uint16_t op;

          if (buf[off] != 0x04)         /* 不是 HCI event，丢掉 */
            {
              off++;
              continue;
            }

          evt_len = buf[off + 2];
          frame = 3 + evt_len;
          if (total - off < frame)
            {
              break;
            }

          if (buf[off + 1] == BT_HCI_EVT_CMD_COMPLETE && evt_len >= 4)
            {
              op = (uint16_t)(buf[off + 4] | (buf[off + 5] << 8));

              if (op == opcode)
                {
                  int status = buf[off + 6];
                  int rplen = evt_len - 4;  /* 去掉 num + opcode(2) + status */

                  if (params != NULL && rplen > 0 && rplen <= 64)
                    {
                      memcpy(params, buf + off + 7, rplen);
                      *plen = rplen;
                    }
                  else if (params != NULL)
                    {
                      *plen = 0;
                    }

                  return status;
                }
            }
          else if (buf[off + 1] == BT_HCI_EVT_CMD_STATUS && evt_len >= 4)
            {
              printf("[BTHCI] (Command Status for opcode 0x%02x%02x)\n",
                     buf[off + 5], buf[off + 4]);
            }

          off += frame;
        }

      if (off > 0)
        {
          memmove(buf, buf + off, total - off);
          total -= off;
        }
    }

  return -3;
}

static const char *bt_lmp_ver_name(int v)
{
  switch (v)
    {
      case 0x03: return "2.0";
      case 0x04: return "2.1";
      case 0x05: return "3.0";
      case 0x06: return "4.0";
      case 0x07: return "4.1";
      case 0x08: return "4.2";
      case 0x09: return "5.0";
      case 0x0a: return "5.1";
      case 0x0b: return "5.2";
      case 0x0c: return "5.3";
      case 0x0d: return "5.4";
      default:   return "?";
    }
}

/* -------------------------------------------------------------------------
 * P1–P6：BR/EDR 配置命令直发探针（BOARD §18，2026-11-04）
 *
 * 背景：`vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 的 emulate 层把
 * WRITE_SCAN_ENABLE / WRITE_CLASS_OF_DEVICE / WRITE_SSP_MODE / SET_EVENT_MASK /
 * WRITE_LOCAL_NAME 等命令吞掉并伪造成功，于是主机以为配好了、控制器没收到，
 * 经典蓝牙对手机不可见。这里绕过驱动层直发，逐条问控制器的真实态度：
 *   status 0x00 → 控制器支持，只要把该条从 emulate 列表摘掉即可；
 *   status 0x01 → 控制器不支持该命令/参数；
 *   status 0x0C → Unknown HCI Command，ROM 没实现，PAN 这条路要看控制器。
 * ------------------------------------------------------------------------- */

static int bt_send_cmd_p(int fd, uint16_t opcode, const unsigned char *p,
                         int plen)
{
  unsigned char cmd[4 + 255];

  cmd[0] = BT_HCI_COMMAND_PKT;
  cmd[1] = (unsigned char)(opcode & 0xff);
  cmd[2] = (unsigned char)(opcode >> 8);
  cmd[3] = (unsigned char)plen;

  if (plen > 0)
    {
      memcpy(cmd + 4, p, (size_t)plen);
    }

  return write(fd, cmd, (size_t)(4 + plen));
}

static void bredr_one(int fd, const char *tag, uint16_t opcode,
                      const unsigned char *p, int plen)
{
  unsigned char cc[8];
  int cclen = 0;
  int st;

  if (bt_send_cmd_p(fd, opcode, p, plen) < 0)
    {
      printf("[BREDR] %-26s opcode=0x%04x -> 写串口失败\n", tag, opcode);
      return;
    }

  st = bt_wait_cc(fd, opcode, cc, &cclen);
  if (st < 0)
    {
      printf("[BREDR] %-26s opcode=0x%04x -> 无 Command Complete (%d)\n",
             tag, opcode, st);
      return;
    }

  printf("[BREDR] %-26s opcode=0x%04x -> status=0x%02x\n", tag, opcode, st);
}

int pw_bt_bredr_probe(void)
{
  static const unsigned char scan_en[1]  = { 0x03 };            /* page+inquiry */
  static const unsigned char class_dev[3] = { 0x24, 0x04, 0x04 };
  static const unsigned char ssp_mode[1] = { 0x01 };
  static const unsigned char evt_mask[8] =
  {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x3d, 0x00
  };

  unsigned char local_name[248];
  int fd;

  memset(local_name, 0, sizeof(local_name));
  memcpy(local_name, "PhyWear", 7);

  fd = open(BT_HCI_DEV, O_RDWR);
  if (fd < 0)
    {
      printf("[BREDR] open %s failed: %d\n", BT_HCI_DEV, errno);
      return -1;
    }

  printf("[BREDR] 直发 HCI（绕过 sf32lb52_bth4.c 的 emulate 层）\n");

  bredr_one(fd, "P1 Reset",              0x0c03, NULL, 0);
  bredr_one(fd, "P2 Write Scan Enable",  0x0c1a, scan_en, 1);
  bredr_one(fd, "P3 Write Class of Dev", 0x0c24, class_dev, 3);
  bredr_one(fd, "P4 Write SSP Mode",     0x0c56, ssp_mode, 1);
  bredr_one(fd, "P5 Set Event Mask",     0x0c01, evt_mask, 8);
  bredr_one(fd, "P6 Write Local Name",   0x0c13, local_name, 248);

  printf("[BREDR] 探针结束（0x00=控制器支持，0x0C=ROM 未实现）\n");
  close(fd);
  return 0;
}


/* P7–P9：只读回读，不发 Reset（Reset 会把 zblue 刚配好的状态清掉）。
 * 用途：改完 emulate 表后确认"主机以为配好了"这次是真的配好了。 */

static void bredr_read(int fd, const char *tag, uint16_t opcode)
{
  unsigned char cc[250];
  int cclen = 0;
  int st;
  int i;

  if (bt_send_cmd_p(fd, opcode, NULL, 0) < 0)
    {
      printf("[BREDR] %-24s -> 写串口失败\n", tag);
      return;
    }

  st = bt_wait_cc(fd, opcode, cc, &cclen);
  if (st < 0)
    {
      printf("[BREDR] %-24s -> 无 Command Complete (%d)\n", tag, st);
      return;
    }

  printf("[BREDR] %-24s status=0x%02x params(%d):", tag, st, cclen);
  for (i = 0; i < cclen && i < 12; i++)
    {
      printf(" %02x", cc[i]);
    }

  if (cclen > 12)
    {
      printf(" ...");
    }

  if (cclen >= 1 && cclen <= 8 && opcode == 0x0c19)
    {
      printf("  (scan_enable)");
    }

  printf("\n");
}

int pw_bt_bredr_state(void)
{
  int fd;

  fd = open(BT_HCI_DEV, O_RDWR);
  if (fd < 0)
    {
      printf("[BREDR] open %s failed: %d\n", BT_HCI_DEV, errno);
      return -1;
    }

  printf("[BREDR] 只读回读控制器当前状态（不发 Reset）\n");
  bredr_read(fd, "P7 Read Scan Enable",     0x0c19);
  bredr_read(fd, "P8 Read Class of Device", 0x0c23);
  bredr_read(fd, "P9 Read Local Name",      0x0c14);
  close(fd);
  return 0;
}

int pw_bt_probe(void)
{
  unsigned char p[80];
  unsigned char feat[8];
  int plen;
  int status;
  int fd;
  int i;
  bool bredr_not_supported;
  bool le_supported;

  fd = open(BT_HCI_DEV, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      printf("[BTHCI] open %s failed: %d (%s)\n", BT_HCI_DEV, errno,
             strerror(errno));
      printf("[BTHCI] 判据①：传输层没起来（CONFIG_UART_BTH4 / 驱动未注册）\n");
      return -1;
    }

  printf("[BTHCI] %s opened\n", BT_HCI_DEV);

  /* ① Reset */

  bt_send_cmd(fd, BT_OP(BT_OGF_HOST_CTL, BT_OCF_RESET));
  status = bt_wait_cc(fd, BT_OP(BT_OGF_HOST_CTL, BT_OCF_RESET), NULL, NULL);
  printf("[BTHCI] HCI_Reset status=%d\n", status);
  if (status < 0)
    {
      printf("[BTHCI] 判据②：Reset 无 Command Complete（rc=%d）\n", status);
      close(fd);
      return -2;
    }

  /* ② Read Local Version Information */

  bt_send_cmd(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_VER));
  plen = 0;
  status = bt_wait_cc(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_VER),
                      p, &plen);
  if (status == 0 && plen >= 8)
    {
      uint16_t manuf = (uint16_t)(p[4] | (p[5] << 8));
      uint16_t sub   = (uint16_t)(p[6] | (p[7] << 8));

      printf("[BTHCI] Local Version: hci_ver=0x%02x hci_rev=0x%04x "
             "lmp_ver=0x%02x(%s) manuf=0x%04x lmp_sub=0x%04x\n",
             p[0], (uint16_t)(p[1] | (p[2] << 8)), p[3],
             bt_lmp_ver_name(p[3]), manuf, sub);
    }
  else
    {
      printf("[BTHCI] Read Local Version failed (status=%d plen=%d)\n",
             status, plen);
    }

  /* ③ Read BD_ADDR */

  bt_send_cmd(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_BD_ADDR));
  plen = 0;
  status = bt_wait_cc(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_BD_ADDR),
                      p, &plen);
  if (status == 0 && plen >= 6)
    {
      printf("[BTHCI] BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x\n",
             p[5], p[4], p[3], p[2], p[1], p[0]);
    }
  else
    {
      printf("[BTHCI] Read BD_ADDR failed (status=%d plen=%d)\n",
             status, plen);
    }

  /* ④ Read Local Supported Features —— 本探针的核心 */

  bt_send_cmd(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_FEAT));
  plen = 0;
  memset(feat, 0, sizeof(feat));
  status = bt_wait_cc(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_FEAT),
                      feat, &plen);
  if (status != 0 || plen < 8)
    {
      printf("[BTHCI] Read Local Features failed (status=%d plen=%d) "
             "—— 控制器不支持该命令则无法判断\n", status, plen);
      close(fd);
      return -4;
    }

  bt_hex("features", feat, 8);

  /* 位号以 NuttX 的 bt_hci.h 为准：BT_LMP_NO_BREDR=0x20 → 特性页 0 bit37，
   * BT_LMP_LE=0x40 → bit38（都在 byte4）。 */

  bredr_not_supported = (feat[4] & 0x20) != 0;   /* bit 37 */
  le_supported        = (feat[4] & 0x40) != 0;   /* bit 38 */

  printf("[BTHCI] LMP bit37 BR/EDR Not Supported           = %d\n",
         bredr_not_supported ? 1 : 0);
  printf("[BTHCI] LMP bit38 LE Supported (Controller)      = %d\n",
         le_supported ? 1 : 0);
  printf("[BTHCI] 结论：%s\n",
         bredr_not_supported
           ? "控制器为 LE-only，无 BR/EDR（PAN/BNEP 在控制器层不成立）"
           : "控制器支持 BR/EDR（PAN 可行性取决于主机栈是否带 BNEP）");

  /* ⑤ Read Local Supported Commands（只报置位数，64 字节太大） */

  bt_send_cmd(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_CMDS));
  plen = 0;
  status = bt_wait_cc(fd, BT_OP(BT_OGF_INFO, BT_OCF_READ_LOCAL_CMDS),
                      p, &plen);
  if (status == 0 && plen >= 64)
    {
      int bits = 0;

      for (i = 0; i < 64; i++)
        {
          unsigned char b = p[i];

          while (b)
            {
              bits += (b & 1);
              b >>= 1;
            }
        }

      printf("[BTHCI] Local Supported Commands: %d bits set (64 B bitmap)\n",
             bits);
    }
  else
    {
      printf("[BTHCI] Read Local Supported Commands failed (status=%d "
             "plen=%d)\n", status, plen);
    }

  close(fd);
  return 0;
}
