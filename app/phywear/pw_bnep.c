/****************************************************************************
 * apps/examples/phywear/pw_bnep.c
 *
 * 蓝牙 PAN（PANU）+ NuttX TAP 桥。设计见
 * phywear-coord/PROPOSALS/dsh/2026-11-04-0045-bnep-design.md
 *
 * ⚠️ 首次上板编译前请先读这段
 * ---------------------------------------------------------------------------
 * 1) 帧编解码在 pw_bnep_frame.c，已用 gcc 在 Linux 上单测 41/41（含畸形帧）。
 *    本文件是"胶水"，**还没在目标板编译过**。原先 6 处 [COMPILE-RISK] 已于
 *    2026-11-04 01:40 逐条比对头文件核实（见各点注释），只剩编译选项差异风险；
 *    若仍报错，按报错改，别改协议语义。
 * 2) 必须先把 CONFIG_NET_TUN_PKTSIZE 从 296 改到 >=1600，否则大包被截断、
 *    ping 小包通、DNS/网页不通（最难查的那种故障）。
 * 3) 这一层不动 nuttx/、不动 vendor/；只在 apps/examples/phywear/ 内。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <net/if.h>
#include <nuttx/net/tun.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/classic/sdp.h>

#include "pw_bnep.h"
#include "pw_bnep_frame.h"

/* SDP 发现用的 net_buf 池由 zblue 的 classic/sdp.c 提供（它本身不是 public
 * 头文件里的符号，所以在用的地方显式 extern —— sal_hfp_hf_interface.c 同款做法）
 */

extern struct net_buf_pool sdp_pool;

/* 我们自己的发送缓冲池：最多 4 个在途帧，每帧按 1600 字节 L2CAP 载荷算 */

NET_BUF_POOL_FIXED_DEFINE(pw_pan_pool, 4, BT_L2CAP_BUF_SIZE(1600), 8, NULL);

#define PW_PAN_FRAME_MAX   1600   /* 单个 BNEP 帧上限（以太 1514 + 头） */
#define PW_PAN_TUN_NAME    "tun0"
#define PW_PAN_PSM_BNEP    0x000F
#define PW_PAN_PSM_UNKNOWN 0x0000

enum pw_pan_state
{
  PW_PAN_IDLE = 0,
  PW_PAN_SDP,
  PW_PAN_L2CAP,
  PW_PAN_SETUP,
  PW_PAN_FILTER,
  PW_PAN_UP,
  PW_PAN_FAIL
};

struct pw_pan_s
{
  enum pw_pan_state    state;
  struct bt_conn      *conn;      /* 我们自己持有一份引用 */
  struct bt_l2cap_br_chan chan;
  uint16_t             psm;
  int                  tunfd;
  pthread_t            thread;
  volatile bool        thread_run;
  volatile bool        thread_started;
  uint32_t             rx_frames;
  uint32_t             tx_frames;
  uint32_t             rx_drop;
  uint32_t             tx_err;
  char                 last_err[64];
};

static struct pw_pan_s g_pan =
{
  .state = PW_PAN_IDLE,
  .tunfd = -1,
  .psm   = PW_PAN_PSM_UNKNOWN,
};

static const char *pan_state_name(enum pw_pan_state s)
{
  switch (s)
    {
      case PW_PAN_IDLE:   return "idle";
      case PW_PAN_SDP:    return "sdp";
      case PW_PAN_L2CAP:  return "l2cap";
      case PW_PAN_SETUP:  return "setup";
      case PW_PAN_FILTER: return "filter";
      case PW_PAN_UP:     return "up";
      case PW_PAN_FAIL:   return "fail";
      default:            return "?";
    }
}

static void pan_fail(const char *why)
{
  snprintf(g_pan.last_err, sizeof(g_pan.last_err), "%s", why);
  g_pan.state = PW_PAN_FAIL;

  /* 必须关掉 TAP：否则下一次 TUNSETIFF 会 ENOMEM（单设备驱动被泄漏的 fd 占住）。 */

  if (g_pan.tunfd >= 0)
    {
      close(g_pan.tunfd);
      g_pan.tunfd = -1;
    }

  printf("[pan] FAIL: %s\n", why);
}

/* ---------------------------------------------------------------------------
 * 发送
 * ------------------------------------------------------------------------- */

static int pan_send_raw(const uint8_t *data, uint32_t len)
{
  struct net_buf *buf;
  int             err;

  if (!g_pan.conn || len == 0 || len > PW_PAN_FRAME_MAX)
    {
      return -EINVAL;
    }

  /* 注意：bt_l2cap_create_pdu() 只在 subsys/bluetooth/host/l2cap_internal.h
   * 里（应用侧拿不到），所以这里用官方给应用侧的等价写法：
   * net_buf_alloc() + net_buf_reserve(BT_L2CAP_CHAN_SEND_RESERVE)。
   * 池大小 BT_L2CAP_BUF_SIZE(1600) 已含 ACL/L2CAP 头，够留这段预留。
   */

  buf = net_buf_alloc(&pw_pan_pool, K_FOREVER);
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  net_buf_reserve(buf, BT_L2CAP_CHAN_SEND_RESERVE);

  if (net_buf_tailroom(buf) < len)
    {
      net_buf_unref(buf);
      return -EMSGSIZE;
    }

  net_buf_add_mem(buf, data, len);

  err = bt_l2cap_chan_send(&g_pan.chan.chan, buf);
  if (err < 0)
    {
      /* 失败时所有权仍在我们手上 */
      net_buf_unref(buf);
      g_pan.tx_err++;
      printf("[pan] send err=%d\n", err);
      return err;
    }

  return 0;
}

static int pan_send_setup_req(void)
{
  uint8_t frame[16];
  int32_t n = pw_bnep_build_setup_req(frame, sizeof(frame), PW_BNEP_UUID_PANU,
                                      PW_BNEP_UUID_NAP);
  if (n < 0)
    {
      return (int)n;
    }

  printf("[pan] send SETUP_CONNECTION_REQ (pANU->NAP)\n");
  return pan_send_raw(frame, (uint32_t)n);
}

static int pan_send_filter(void)
{
  static const struct pw_bnep_ftype ranges[] =
  {
    { PW_BNEP_ETH_IPV4, PW_BNEP_ETH_IPV4 },
    { PW_BNEP_ETH_ARP,  PW_BNEP_ETH_ARP  },
  };

  uint8_t frame[16];
  int32_t n = pw_bnep_build_filter_set(frame, sizeof(frame), ranges,
                                       (uint8_t)(sizeof(ranges) / sizeof(ranges[0])));
  if (n < 0)
    {
      return (int)n;
    }

  printf("[pan] send FILTER_NET_TYPE_SET (ipv4+arp)\n");
  return pan_send_raw(frame, (uint32_t)n);
}

/* ---------------------------------------------------------------------------
 * TUN（TAP 模式：以太帧进出）
 * ------------------------------------------------------------------------- */

static int pan_tun_open(void)
{
  struct ifreq ifr;
  int          fd;

  fd = open("/dev/tun", O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      printf("[pan] open /dev/tun err=%d\n", errno);
      return -errno;
    }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, PW_PAN_TUN_NAME, sizeof(ifr.ifr_name) - 1);
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;          /* [已核实] net/if.h: #define ifr_flags ifr_ifru.ifru_flags (uint32_t) */

  if (ioctl(fd, TUNSETIFF, (unsigned long)&ifr) < 0)
    {
      printf("[pan] TUNSETIFF err=%d\n", errno);
      close(fd);
      return -errno;
    }

  g_pan.tunfd = fd;
  printf("[pan] tun ready: %s (tap)\n", ifr.ifr_name);

  /* 注意：接口地址由 `dhcpc -i tun0` 或手动 ifconfig 决定，本模块不配 IP。
   * 建议在 `phywear pan on` 成功后由 NSH 侧跑一次 dhcpc（见 BOARD §12 验收）。
   */

  return 0;
}

static void pan_tun_close(void)
{
  if (g_pan.tunfd >= 0)
    {
      close(g_pan.tunfd);
      g_pan.tunfd = -1;
    }
}

/* ---------------------------------------------------------------------------
 * 收帧（L2CAP 回调，跑在 BT host 的 rx 线程上）
 * ------------------------------------------------------------------------- */

static int pan_on_frame(const uint8_t *data, uint32_t len)
{
  struct pw_bnep_hdr hdr;
  int32_t            rc;

  rc = pw_bnep_parse(data, len, &hdr);
  if (rc < 0)
    {
      g_pan.rx_drop++;
      return 0;
    }

  if (hdr.type == PW_BNEP_CONTROL)
    {
      switch (hdr.ctl)
        {
          case PW_BNEP_CTL_SETUP_RSP:
            {
              uint16_t code = 0xffff;
              if (pw_bnep_parse_setup_rsp(data, len, &code) == 0)
                {
                  if (code == 0)
                    {
                      printf("[pan] BNEP setup rsp=0x0000 ok\n");
                      g_pan.state = PW_PAN_FILTER;
                      pan_send_filter();
                    }
                  else
                    {
                      printf("[pan] BNEP setup rsp=0x%04x -> fail\n", code);
                      pan_fail("bnep setup rejected");
                    }
                }
            }
            break;

          case PW_BNEP_CTL_FILTER_RSP:
            {
              uint16_t code = 0xffff;
              if (pw_bnep_parse_filter_rsp(data, len, &code) == 0)
                {
                  if (code == 0)
                    {
                      printf("[pan] filter ok -> UP\n");
                      g_pan.state = PW_PAN_UP;
                    }
                  else
                    {
                      printf("[pan] filter rsp=0x%04x -> fail\n", code);
                      pan_fail("filter rejected");
                    }
                }
            }
            break;

          default:
            printf("[pan] control ctl=0x%02x len=%u (ignored)\n", hdr.ctl,
                   (unsigned)hdr.paylen);
            break;
        }

      return 0;
    }

  if (hdr.eth == NULL || hdr.ethlen == 0)
    {
      g_pan.rx_drop++;
      return 0;
    }

  if (g_pan.tunfd < 0)
    {
      g_pan.rx_drop++;
      return 0;
    }

  if (write(g_pan.tunfd, hdr.eth, hdr.ethlen) != (ssize_t)hdr.ethlen)
    {
      g_pan.rx_drop++;
      return 0;
    }

  g_pan.rx_frames++;
  return 0;
}

static int pan_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
  /* [修复#2] rx 线程栈只有 CONFIG_BT_RX_STACK_SIZE(1200) B，
   * 1600 B 局部数组必然压爆；BR/EDR 的 recv 是单线程同步回调，静态缓冲安全。 */
  static uint8_t s_frame[PW_PAN_FRAME_MAX];
  uint32_t total;
  uint32_t off = 0;
  struct net_buf *f;

  (void)chan;

  total = net_buf_frags_len(buf);               /* [已核实] net_buf.h:2711 static inline size_t */
  if (total > sizeof(s_frame))
    {
      g_pan.rx_drop++;
      /* [修复#1] BR/EDR host 在回调后无条件 net_buf_unref(buf)
       * (l2cap_br.c:2104-2105)，应用不得再释放，否则 refcount 下溢。 */
      return 0;
    }

  /* 逐片拷贝：收到的 PDU 各片 data 都指向本片载荷起点 */

  for (f = buf; f != NULL && off < total; f = f->frags)
    {
      uint32_t n = f->len;
      if (n > total - off)
        {
          n = total - off;
        }

      memcpy(&s_frame[off], f->data, n);
      off += n;
    }

  /* [修复#1] 同上：所有权在 host，应用不再 unref。 */

  if (off != total)
    {
      g_pan.rx_drop++;
      return 0;
    }

  return pan_on_frame(s_frame, total);
}

static void pan_l2cap_connected(struct bt_l2cap_chan *chan)
{
  (void)chan;
  printf("[pan] L2CAP connected\n");
  g_pan.state = PW_PAN_SETUP;
  pan_send_setup_req();
}

static void pan_l2cap_disconnected(struct bt_l2cap_chan *chan)
{
  (void)chan;
  printf("[pan] L2CAP disconnected\n");
  if (g_pan.state == PW_PAN_UP || g_pan.state == PW_PAN_SETUP ||
      g_pan.state == PW_PAN_FILTER)
    {
      g_pan.state = PW_PAN_IDLE;
    }
}

static const struct bt_l2cap_chan_ops g_pan_l2cap_ops =
{
  .connected    = pan_l2cap_connected,
  .disconnected = pan_l2cap_disconnected,
  .recv         = pan_l2cap_recv,
};

/* ---------------------------------------------------------------------------
 * SDP：查 NAP 的 L2CAP PSM
 * ------------------------------------------------------------------------- */

/* 直接连 BNEP 的固定 PSM（不查 SDP）。 */

static int pan_l2cap_start(struct bt_conn *conn, uint16_t psm)
{
  int err;

  memset(&g_pan.chan, 0, sizeof(g_pan.chan));
  g_pan.chan.chan.ops = &g_pan_l2cap_ops;
  /* [修复#3] l2cap_br.c:769-770 只在 rx.mtu != 672 时下发 MTU 选项；
   * memset 后是 0 → 我们会在 wire 上宣告 MTU=0，对端一帧都不发。 */
  g_pan.chan.rx.mtu = 1691;
  /* [修复#13] 安全等级：memset 后是 0，l2cap_br.c:1733-1735 会抬到 L1(不加密)。 */
  g_pan.chan.required_sec_level = BT_SECURITY_L1; /* [实验] 先不做加密，验证 BNEP 数据面 */
  g_pan.psm = psm;
  g_pan.state = PW_PAN_L2CAP;

  printf("[pan] L2CAP connect psm=0x%04x\n", psm);
  err = bt_l2cap_chan_connect(conn, &g_pan.chan.chan, psm);
  if (err < 0)
    {
      printf("[pan] l2cap connect err=%d\n", err);
      pan_fail("l2cap connect failed");
    }

  return err;
}

static uint8_t pan_sdp_done(struct bt_conn *conn, struct bt_sdp_client_result *result,
                            const struct bt_sdp_discover_params *params)
{
  uint16_t psm = PW_PAN_PSM_UNKNOWN;
  int      err;

  (void)params;

  if (result == NULL || result->resp_buf == NULL)
    {
      pan_fail("sdp: no NAP record (phone hotspot off?)");
      return BT_SDP_DISCOVER_UUID_STOP;
    }

  /* [已核实] classic/sdp.h:654 int bt_sdp_get_proto_param(const struct net_buf *, enum bt_sdp_proto, uint16_t *)；BT_SDP_PROTO_L2CAP=0x0100 见 :639 */

  err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_L2CAP, &psm);
  if (err != 0)
    {
      pan_fail("sdp: cannot parse L2CAP psm");
      return BT_SDP_DISCOVER_UUID_STOP;
    }

  g_pan.psm = psm;
  printf("[pan] SDP ok psm=0x%04x\n", psm);

  pan_l2cap_start(conn, psm);
  (void)err;

  return BT_SDP_DISCOVER_UUID_STOP;
}

static void pan_sdp_disconnected(struct bt_conn *conn,
                                 const struct bt_sdp_discover_params *params)
{
  (void)conn;
  (void)params;
  if (g_pan.state == PW_PAN_SDP)
    {
      pan_fail("sdp disconnected");
    }
}

static struct bt_sdp_discover_params g_pan_sdp_params =
{
  .func         = pan_sdp_done,
  .disconnected = pan_sdp_disconnected,
  .pool         = &sdp_pool,
  .uuid         = BT_UUID_DECLARE_16(PW_BNEP_UUID_NAP),
  .type         = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
};

/* ---------------------------------------------------------------------------
 * TUN → BNEP 的发送线程
 * ------------------------------------------------------------------------- */

static void *pan_tun_thread(void *arg)
{
  uint8_t eth[PW_PAN_FRAME_MAX];
  uint8_t wire[PW_PAN_FRAME_MAX + 8];

  (void)arg;
  printf("[pan] tun thread start\n");

  while (g_pan.thread_run)
    {
      struct pollfd pfd;
      int           ret;
      ssize_t       n;

      pfd.fd      = g_pan.tunfd;
      pfd.events  = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, 500);
      if (ret <= 0)
        {
          continue;
        }

      n = read(g_pan.tunfd, eth, sizeof(eth));
      if (n <= 0)
        {
          continue;
        }

      if (g_pan.state != PW_PAN_UP)
        {
          /* 还没握手完，帧先丢（对端这时也不会发东西给我们） */
          continue;
        }

      {
        int32_t m = pw_bnep_build_data(wire, sizeof(wire), eth, (uint32_t)n);
        if (m > 0 && pan_send_raw(wire, (uint32_t)m) == 0)
          {
            g_pan.tx_frames++;
          }
      }
    }

  printf("[pan] tun thread exit\n");
  return NULL;
}

/* ---------------------------------------------------------------------------
 * 对外 API
 * ------------------------------------------------------------------------- */

static int pan_thread_start(void)
{
  pthread_attr_t attr;
  int            err;

  if (g_pan.thread_started)
    {
      return 0;
    }

  g_pan.thread_run = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 6144);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

  err = pthread_create(&g_pan.thread, &attr, pan_tun_thread, NULL);
  pthread_attr_destroy(&attr);
  if (err != 0)
    {
      printf("[pan] pthread_create err=%d\n", err);
      g_pan.thread_run = false;
      return -err;
    }

  g_pan.thread_started = true;
  return 0;
}

static void pan_reset(void)
{
  if (g_pan.conn != NULL)
    {
      bt_conn_unref(g_pan.conn);
      g_pan.conn = NULL;
    }

  g_pan.state = PW_PAN_IDLE;
  g_pan.psm   = PW_PAN_PSM_UNKNOWN;
}

static int pan_begin_with_conn(void);

int pw_pan_start(const char *peer_mac)
{
  bt_addr_t addr;
  int       err;

  if (peer_mac == NULL)
    {
      return -EINVAL;
    }

  if (g_pan.state != PW_PAN_IDLE && g_pan.state != PW_PAN_FAIL)
    {
      printf("[pan] already %s\n", pan_state_name(g_pan.state));
      return -EBUSY;
    }

  pan_reset();

  err = bt_addr_from_str(peer_mac, &addr);      /* [已核实] addr.h:284 int (const char *, bt_addr_t *)，0 / -EINVAL */
  if (err != 0)
    {
      printf("[pan] bad mac '%s'\n", peer_mac);
      return -EINVAL;
    }

  g_pan.conn = bt_conn_lookup_addr_br(&addr);   /* [已核实] conn.h:756 内联→bt_conn_lookup_addr_br_mc(0,peer)；需 CONFIG_BT_ORIGINAL_API=y；返回引用须 bt_conn_unref() */
  if (g_pan.conn == NULL)
    {
      /* 没有现成 ACL：提示先配对/连接。真机上可换成 bt_conn_create_br() 主动发起 */
      printf("[pan] no BR/EDR connection to %s (pair first)\n", peer_mac);
      pan_fail("no acl link");
      return -ENOTCONN;
    }

  return pan_begin_with_conn();
}

/* 复用 pw_pan_start 剩下的步骤（tun + SDP + 线程），conn 已存在。 */

static int pan_begin_with_conn(void)
{
  int err;

  if (pan_tun_open() < 0)
    {
      pan_fail("tun open failed");
      return -EIO;
    }

  err = pan_l2cap_start(g_pan.conn, PW_PAN_PSM_BNEP);
  pan_thread_start();
  return err;
}

/* 直接拿 ACL 连接对象起动 PANU（不解析地址字符串）。 */

int pw_pan_start_conn(struct bt_conn *conn)
{
  if (conn == NULL)
    {
      return -EINVAL;
    }

  if (g_pan.state != PW_PAN_IDLE && g_pan.state != PW_PAN_FAIL)
    {
      printf("[pan] already %s\n", pan_state_name(g_pan.state));
      return -EBUSY;
    }

  pan_reset();
  g_pan.conn = bt_conn_ref(conn);       /* 自己持一份引用，pan_reset 里会 unref */
  printf("[pan] start via conn\n");
  return pan_begin_with_conn();
}

/* [PAN-BLE] 用 BLE 文本通道触发 PANU：没有 ACL 就先建 ACL。
 * ACL 建好后 pw_connected() 里的 pw_pan_auto_start() 会接手。 */

int pw_pan_connect(const char *peer_mac)
{
  bt_addr_t addr;
  FAR struct bt_conn *conn;
  int err;

  if (peer_mac == NULL || peer_mac[0] == '\0')
    {
      return -EINVAL;
    }

  /* "pan:off" —— 停掉 PANU 并释放连接引用（调试/重试用）。 */
  if (strcmp(peer_mac, "off") == 0)
    {
      printf("[pan] connect: stop requested\n");
      pw_pan_stop();
      return 0;
    }

  err = bt_addr_from_str(peer_mac, &addr);

  conn = bt_conn_lookup_addr_br(&addr);
  if (conn != NULL)
    {
      bt_conn_unref(conn);
      printf("[pan] connect: ACL exists, start PANU to %s\n", peer_mac);
      return pw_pan_start(peer_mac);
    }

  printf("[pan] connect: no ACL, creating BR/EDR link to %s\n", peer_mac);
  pw_pan_stop();                        /* 释放上一次可能残留的引用 */
  conn = bt_conn_create_br(&addr, BT_BR_CONN_PARAM_DEFAULT);
  if (conn == NULL)
    {
      printf("[pan] connect: bt_conn_create_br failed\n");
      return -EIO;
    }

  bt_conn_unref(conn);
  return -EAGAIN;
}

/* [PAN-POLL] 不依赖连接回调：每 2 s 看一次 BR/EDR 连接，有就起 PANU。 */

static void pan_poll_cb(struct bt_conn *conn, void *data)
{
  FAR struct bt_conn **out = (FAR struct bt_conn **)data;

  if (*out == NULL)
    {
      *out = bt_conn_ref(conn);
    }
}

static void *pw_pan_poll_thread(void *arg)
{
  (void)arg;

  for (;;)
    {
      sleep(2);

      if (g_pan.state == PW_PAN_IDLE || g_pan.state == PW_PAN_FAIL)
        {
          FAR struct bt_conn *conn = NULL;
          int rc;

          bt_conn_foreach(BT_CONN_TYPE_BR, pan_poll_cb, &conn);
          if (conn != NULL)
            {
              printf("[pan] poll: BR conn found, settle 4s\n");
              sleep(4);
              printf("[pan] poll: starting PANU\n");
              rc = pw_pan_start_conn(conn);
              bt_conn_unref(conn);
              printf("[pan] poll: rc=%d\n", rc);
            }
        }
    }

  return NULL;
}

void pw_pan_poll_start(void)
{
  static bool started;
  pthread_t th;

  if (started)
    {
      return;
    }

  if (pthread_create(&th, NULL, pw_pan_poll_thread, NULL) == 0)
    {
      pthread_detach(th);
      started = true;
      printf("[pan] poll thread started (2 s)\n");
    }
}

/* [PAN-SEC] 对齐 小智 的时序：加密/配对完成后再等 3 s 才连 PAN
 * （SDK 例程注释原话 "Trigger PAN connection after PAN_TIMER_MS period to
 * avoid SDP confliction"，PAN_TIMER_MS = 3000）。 */

struct pan_delay_arg
{
  struct bt_conn *conn;
};

static void *pan_delay_thread(void *arg)
{
  struct pan_delay_arg *a = (struct pan_delay_arg *)arg;
  int rc;

  sleep(3);
  printf("[pan] security-ready: starting PANU after 3 s\n");
  rc = pw_pan_start_conn(a->conn);
  printf("[pan] security-ready: rc=%d\n", rc);
  bt_conn_unref(a->conn);
  free(a);
  return NULL;
}

void pw_pan_start_on_security(struct bt_conn *conn)
{
  struct pan_delay_arg *a;
  pthread_t th;

  if (conn == NULL)
    {
      return;
    }

  if (g_pan.state != PW_PAN_IDLE && g_pan.state != PW_PAN_FAIL)
    {
      return;
    }

  a = (struct pan_delay_arg *)calloc(1, sizeof(*a));
  if (a == NULL)
    {
      return;
    }

  a->conn = bt_conn_ref(conn);
  if (pthread_create(&th, NULL, pan_delay_thread, a) == 0)
    {
      pthread_detach(th);
    }
  else
    {
      bt_conn_unref(a->conn);
      free(a);
    }
}

int pw_pan_stop(void)
{
  if (g_pan.chan.chan.conn != NULL)
    {
      bt_l2cap_chan_disconnect(&g_pan.chan.chan);
    }

  g_pan.thread_run = false;
  if (g_pan.thread_started)
    {
      pthread_join(g_pan.thread, NULL);
      g_pan.thread_started = false;
    }

  pan_tun_close();
  pan_reset();
  printf("[pan] stopped\n");
  return 0;
}

bool pw_pan_is_up(void)
{
  return g_pan.state == PW_PAN_UP;
}

int pw_pan_status(char *buf, size_t len)
{
  if (buf == NULL || len == 0)
    {
      return -EINVAL;
    }

  return snprintf(buf, len,
                  "state=%s psm=0x%04x tun=%s rx=%lu tx=%lu drop=%lu txerr=%lu%s%s",
                  pan_state_name(g_pan.state), g_pan.psm,
                  g_pan.tunfd >= 0 ? PW_PAN_TUN_NAME : "-",
                  (unsigned long)g_pan.rx_frames, (unsigned long)g_pan.tx_frames,
                  (unsigned long)g_pan.rx_drop, (unsigned long)g_pan.tx_err,
                  g_pan.last_err[0] ? " err=" : "",
                  g_pan.last_err[0] ? g_pan.last_err : "");
}
