/****************************************************************************
 * apps/examples/phywear/pw_btgatt.c
 *
 * 蓝牙阶段 B-2：注册 GATT 服务 + 开可被发现广播。
 *
 * 设计（尽量少变量、每一步都有 rc 可判）：
 *   Service  PhyWear Physics      e0f1a000-1b2c-4d5e-8f90-a1b2c3d4e5f6
 *     ├─ Sensor  READ | NOTIFY    e0f1a001-…  ← 16 B 小端传感器快照
 *     │    └─ CCC（订阅开关）
 *     ├─ Command WRITE | WRITE_NR e0f1a002-…  ← 手机写命令，串口回显
 *     └─ Text    READ | WRITE | WRITE_NR | NOTIFY  e0f1a003-…
 *          └─ CCC        ← 「手表版蓝牙串口」：手机写自由文本、手表回 echo、
 *                          手表也能主动 notify 文本给手机（页面按 [TX]/[RX] 显示）
 *
 * Sensor 包（16 B，全部小端）：
 *   int16 ax, ay, az   mg
 *   int16 gx, gy, gz   mdps/10（0.01 dps 单位，±327 dps 不溢出）
 *   uint16 lux         估算照度
 *   uint16 cmd_count   收到的命令条数（手机写一条就 +1，用来肉眼确认「写得进来」）
 *
 * 为什么不用「采样线程」而用 k_work_delayable：
 *   ① 采样走 pw_sensors_read_*_oneshot()，它**自己 open/close** /dev 设备，
 *      因此在哪个 task_group 里跑都合法（这一点刚在 H4 上踩过坑，见 docs/16）；
 *   ② bt_gatt_notify() 最终只走到 h4_send() → tx_fifo，不碰 fd；
 *   ③ 只在**有订阅者**时才采样，避免空转。
 *
 * 跨线程（2026-09-18 晚新增 UI 后必须牢记）：
 *   本文件里的回调（连接/断开/CCC/写特征）在 **Zephyr 系统工作队列**线程上，
 *   而界面在 **phywear 主线程**上。所以回调里只允许写 `g_link` 里的标志与
 *   环形日志，**绝不调用 LVGL**；界面侧只读 `pw_bt_link()`。
 *   详见 pw_btgatt.h 顶部的约定说明。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/crypto.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "phywear_sensors.h"
#include "pw_btgatt.h"

/* UUID 按 LSB-first 排布（BT_UUID_INIT_128 的字节序） */

static const struct bt_uuid_128 pw_svc_uuid = BT_UUID_INIT_128(
  0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0x90, 0x8f,
  0x5e, 0x4d, 0x2c, 0x1b, 0x00, 0xa0, 0xf1, 0xe0);

static const struct bt_uuid_128 pw_sensor_uuid = BT_UUID_INIT_128(
  0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0x90, 0x8f,
  0x5e, 0x4d, 0x2c, 0x1b, 0x01, 0xa0, 0xf1, 0xe0);

static const struct bt_uuid_128 pw_cmd_uuid = BT_UUID_INIT_128(
  0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0x90, 0x8f,
  0x5e, 0x4d, 0x2c, 0x1b, 0x02, 0xa0, 0xf1, 0xe0);

static const struct bt_uuid_128 pw_text_uuid = BT_UUID_INIT_128(
  0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0x90, 0x8f,
  0x5e, 0x4d, 0x2c, 0x1b, 0x03, 0xa0, 0xf1, 0xe0);

/* ── 属性表的索引与前置声明 ─────────────────────────────────────────────
 * 索引必须**具名**：回调里到处是 `&pw_attrs[7]` 这种写法，写裸数字时
 * 一旦有人在中间插一条特征，索引会静默错位（读到别人的值句柄），
 * 而编译、自检、手机侧全都不报错 —— 属于最难查的一类。
 * 前置声明是为了让"手表→手机"的发送工作项能先于表定义引用它。 */

#define PW_ATTR_SENSOR_VALUE   2    /* e0f1a001 值句柄（特征声明在 1） */
#define PW_ATTR_CMD_VALUE      5    /* e0f1a002 值句柄 */
#define PW_ATTR_TEXT_VALUE     7    /* e0f1a003 值句柄（新增，追加在表尾） */

static struct bt_gatt_attr pw_attrs[];

/* ── 状态 ───────────────────────────────────────────────────────────── */

struct pw_bt_pkt_s
{
  int16_t  ax, ay, az;      /* mg */
  int16_t  gx, gy, gz;      /* mdps/10 */
  uint16_t lux;
  uint16_t cmd_count;
} __attribute__((packed));

static struct pw_bt_pkt_s   g_pkt;
static uint8_t              g_nfy_on;       /* 传感器 CCC 是否已订阅 */
static uint8_t              g_text_sub;     /* 文本特征 CCC 是否已订阅 */
static uint16_t             g_cmd_count;
static char                 g_last_cmd[32];
static struct k_work_delayable g_sample_work;

/* UI 可见的链路状态 + 文本串口日志（写入方是 BT 回调，见 pw_btgatt.h 约定） */
static struct pw_bt_link_s  g_link;

/* 手表 → 手机 的文本发送队列（只放一条，连点就 -EBUSY） */
static struct k_work       g_text_work;
static struct k_work       g_adv_work;      /* 断线后重开广播（见下） */
static struct bt_conn     *g_conn;          /* 当前连接（只用来查实时参数） */

/* 连接参数请求的总开关：**为了做同代码 A/B**（见下）。
 * 实测结论（2026-09-18，BlueZ 当中心设备）：请求开与关，协商结果完全一样
 * （itv=24 → 30.0 ms、lat=0、sto=400）⇒ **对 BlueZ 是 no-op**。
 * 仍然默认打开：手机当中心设备时可能认这个请求，代价只是一个 L2CAP 信令包。 */
#ifndef PW_BT_FAST_CONN
#define PW_BT_FAST_CONN 1
#endif

/* 主动交换 ATT MTU 的开关（同样为了同代码 A/B）。
 * 实测结论：BlueZ 自己也会把 MTU 拉到 517 —— 把这个宏关掉，读回来的
 * 仍是 `mtu=517`（我一开始误以为是自己的交换起作用了）。保持打开是因为
 * 其它中心设备（手机）不一定主动交换。 */
#ifndef PW_BT_MTU_EXCH
#define PW_BT_MTU_EXCH 1
#endif
static void pw_adv_restart(struct k_work *work);
static void pw_bt_log(uint8_t dir, FAR const char *text);
static char                g_text_out[PW_BT_TEXT_MAX + 1];
static uint8_t             g_text_pending;

#define PW_SAMPLE_MS   500

/* ── 链路层优化：连接参数 + ATT MTU（2026-09-18 深夜）────────────────────
 *
 * 背景（有实测基线，不是拍脑袋）：默认参数下，宿主侧量到的应用层
 * "写一条文本→收回 echo" 往返是 **中位 135.0 ms / 最小 109.4 / 最大 178.9**，
 * 定长往返吞吐 **7.3 条/s**（12 B 载荷）；而设备页面上显示的 `MTU 23`
 * 其实是**连接瞬间的快照**，并不是协商后的真实值。
 *
 * 两件事：
 *   ① 连接后**主动请求**更短的连接间隔（15~30 ms、slave latency 0）；
 *      BLE 规范最小间隔是 12×1.25 ms = 15 ms，我们把下限就设在这儿。
 *   ② 注册 ATT MTU 更新回调，把**真实协商值**记下来并显示 —— 顺带修掉
 *      "页面一直显示 MTU 23"这个显示错误。同时主动发起一次 MTU 交换，
 *      把上限拉高（默认 23 时一次写只能带 20 B 载荷，做字节管道/PPP 会很痛苦）。
 *
 * 注意：连接参数最终由**中心设备**决定（我们这边只是发 L2CAP 参数更新请求），
 * 所以协商结果要从 `le_param_updated` 读出来，**不能假定请求被接受**。
 */
static const struct bt_le_conn_param pw_conn_param =
{
  .interval_min = 12,   /* 12 × 1.25 ms = 15 ms（规范下限） */
  .interval_max = 24,   /* 24 × 1.25 ms = 30 ms */
  .latency = 0,         /* 每个连接事件都听主设备：延迟优先 */
  .timeout = 400,       /* 400 × 10 ms = 4 s 监督超时 */
};

static void pw_att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
  (void)conn;
  g_link.mtu = tx;
  printf("[bt] mtu updated tx=%u rx=%u\n", (unsigned)tx, (unsigned)rx);
  pw_bt_log(PW_BT_DIR_SYS, "mtu updated");
}

static struct bt_gatt_cb pw_gatt_cb =
{
  .att_mtu_updated = pw_att_mtu_updated,
};

/* MTU 交换的应答回调。这里**只打结果**：真正要用的 MTU 值统一由
 * `att_mtu_updated` 落到 g_link.mtu —— 两个来源都写同一处容易打架。 */
static void pw_mtu_exchanged(struct bt_conn *conn, uint8_t err,
                             struct bt_gatt_exchange_params *params)
{
  (void)conn;
  (void)params;
  printf("[bt] exchange mtu done err=%u\n", err);
}

static struct bt_gatt_exchange_params pw_mtu_params =
{
  .func = pw_mtu_exchanged,
};

/* ── 链路状态 / 文本日志（BT 线程写，UI 线程读）──────────────────────── */

FAR const struct pw_bt_link_s *pw_bt_link(void)
{
  return &g_link;
}

/* 往环形日志里追加一行。
 *
 * 只做整字宽写入 + 最后自增 seq：seq 是"这一行已写完"的标记，
 * UI 看到 seq 变了才重画。极端情况下 UI 可能读到半行（见头文件说明），
 * 下一次刷新会自我修正 —— 换取的收益是 HCI 回调里不加锁、不阻塞。 */
static void pw_bt_log(uint8_t dir, FAR const char *text)
{
  uint32_t n = g_link.seq;
  FAR struct pw_bt_logline_s *ln = &g_link.log[n % PW_BT_LOG_LINES];
  size_t i;

  for (i = 0; i < PW_BT_TEXT_MAX && text[i] != '\0'; i++)
    {
      char ch = text[i];

      /* 串口日志只放可打印 ASCII：控制字符会让 LVGL 的文本渲染错位，
       * 非 ASCII（UTF-8 多字节）在页面上也要按字节截断才不会半个字。 */
      ln->text[i] = (ch >= 0x20 && ch < 0x7f) ? ch : '.';
    }

  ln->text[i] = '\0';
  ln->dir = dir;
  g_link.seq = n + 1;
}

static void pw_bt_log_reset(void)
{
  memset(g_link.log, 0, sizeof(g_link.log));
  g_link.seq = 1;
  g_link.log[0].dir = PW_BT_DIR_SYS;
  strcpy(g_link.log[0].text, "PhyWear BT console");
}

/* 手表 → 手机：真正发送（跑在 Zephyr 工作队列线程上） */
static void pw_text_work_handler(struct k_work *work)
{
  int rc;

  (void)work;

  if (!g_text_pending)
    {
      return;
    }

  g_text_pending = 0;

  /* 句柄不变式：attrs[6]=chrc decl, [7]=text value, [8]=CCC
   * （新增特征追加在表尾，所以旧的 0x0010/0x0011/0x0013 都不受影响） */
  rc = bt_gatt_notify(NULL, &pw_attrs[PW_ATTR_TEXT_VALUE], g_text_out,
                      strlen(g_text_out));
  printf("[bt] text tx rc=%d: '%s'\n", rc, g_text_out);

  if (rc == 0)
    {
      g_link.tx++;
    }
}

int pw_bt_send_text(FAR const char *text)
{
  size_t n;

  if (text == NULL)
    {
      return -EINVAL;
    }

  n = strlen(text);
  if (n == 0 || n > PW_BT_TEXT_MAX)
    {
      return -EINVAL;
    }

  if (!g_link.connected)
    {
      return -ENOTCONN;
    }

  /* 上一条还没发完就别覆盖 —— 按钮连点时这是正常拒绝，不是故障。 */
  if (k_work_busy_get(&g_text_work) != 0)
    {
      return -EBUSY;
    }

  memcpy(g_text_out, text, n + 1);
  g_text_pending = 1;
  pw_bt_log(PW_BT_DIR_TX, text);
  k_work_submit(&g_text_work);
  return 0;
}

/* ── GATT 回调 ──────────────────────────────────────────────────────── */

static void pw_sample_once(int strict);

/* 读的时候**现采一次**：手机点一下"读"就应拿到当下这一秒的值，
 * 而不是上一轮 notify 的旧值。（采样最坏 ~150 ms，读是用户触发的，可接受。） */
static ssize_t pw_read_sensor(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset)
{
  if (offset == 0)
    {
      pw_sample_once(1);
    }

  return bt_gatt_attr_read(conn, attr, buf, len, offset, &g_pkt,
                           sizeof(g_pkt));
}

static ssize_t pw_write_cmd(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags)
{
  size_t n = len;

  (void)conn;
  (void)attr;
  (void)flags;

  if (offset != 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

  if (n > sizeof(g_last_cmd) - 1)
    {
      n = sizeof(g_last_cmd) - 1;
    }

  memcpy(g_last_cmd, buf, n);
  g_last_cmd[n] = '\0';
  g_cmd_count++;
  g_pkt.cmd_count = g_cmd_count;

  printf("[bt] cmd #%u (%u B): '%s'\n", (unsigned)g_cmd_count, (unsigned)len,
         g_last_cmd);

  /* 约定的最小命令集（手机侧写 ASCII） */

  if (strcmp(g_last_cmd, "notify") == 0)
    {
      g_nfy_on = 1;
      printf("[bt] cmd: notify forced on\n");
    }
  else if (strcmp(g_last_cmd, "ping") == 0)
    {
      printf("[bt] cmd: pong\n");
    }
  else if (strcmp(g_last_cmd, "selftest") == 0)
    {
      printf("[bt] cmd: selftest ok (设备侧自检，非手机写入)\n");
    }

  return len;
}

static void pw_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
  (void)attr;
  g_nfy_on = (value & BT_GATT_CCC_NOTIFY) ? 1 : 0;
  g_link.subscribed = g_nfy_on || g_text_sub;
  printf("[bt] ccc changed -> notify %s\n", g_nfy_on ? "ON" : "OFF");
}

/* ── 文本特征 e0f1a003：「手表版蓝牙串口」──────────────────────────────
 *
 * 读：回一句状态文本（手机端可以直接点"读"看设备状态，不必解析二进制）
 * 写：① 计数 + 打串口原文（证据）② 记进 [RX] 日志（页面显示）
 *     ③ **回一条 echo 给手机** —— 这就是"连通性测试"的判据：
 *        手机写进去、又原样收回来，说明两个方向的空口链路都通。
 */
static ssize_t pw_text_read(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, void *buf,
                            uint16_t len, uint16_t offset)
{
  char st[80];
  int n;

  /* 连接参数从**实时**查询拿（bt_conn_get_info），而不是只依赖
   * le_param_updated 回调 —— 实测那个回调在本链路（中心设备是 BlueZ）上
   * 一次都没触发过，只靠它就会永远不知道真实间隔是多少。
   * 放"读"里是有意的：宿主读一次 …a003 就能拿到当下的真实参数，
   * A/B 比较时不用回去翻串口日志。 */
  uint16_t itv = 0;
  uint16_t lat = 0;
  uint16_t sto = 0;

  if (g_conn != NULL)
    {
      struct bt_conn_info ci;

      if (bt_conn_get_info(g_conn, &ci) == 0)
        {
          itv = ci.le.interval;
          lat = ci.le.latency;
          sto = ci.le.timeout;
        }
    }

  n = snprintf(st, sizeof(st),
               "PhyWear bt conn=%u sub=%u rx=%u tx=%u echo=%u mtu=%u "
               "itv=%u lat=%u sto=%u",
               (unsigned)g_link.connected, (unsigned)g_link.subscribed,
               (unsigned)g_link.rx, (unsigned)g_link.tx,
               (unsigned)g_link.echo, (unsigned)g_link.mtu,
               (unsigned)itv, (unsigned)lat, (unsigned)sto);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, st, (uint16_t)n);
}

static ssize_t pw_text_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags)
{
  char in[PW_BT_TEXT_MAX + 1];
  char echo[PW_BT_TEXT_MAX + 16];
  size_t n = len;

  (void)conn;
  (void)attr;
  (void)flags;

  if (offset != 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

  if (n > PW_BT_TEXT_MAX)
    {
      n = PW_BT_TEXT_MAX;
    }

  memcpy(in, buf, n);
  in[n] = '\0';

  g_link.rx++;
  pw_bt_log(PW_BT_DIR_RX, in);
  printf("[bt] msg in #%u (%u B): '%s'\n", (unsigned)g_link.rx,
         (unsigned)len, in);

  /* 回 echo：这是"手机写进来了"与"手表能主动发出去"两件事的合并判据。
   * 没订阅就不发（发了也没人收），但**仍然在日志里记一条**，
   * 这样页面/串口上能看出"收到了但对方没订阅"。 */
  snprintf(echo, sizeof(echo), "echo: %s", in);

  if (g_text_sub)
    {
      int rc = bt_gatt_notify(NULL, &pw_attrs[PW_ATTR_TEXT_VALUE], echo,
                              strlen(echo));

      printf("[bt] echo tx rc=%d: '%s'\n", rc, echo);
      if (rc == 0)
        {
          g_link.echo++;
          g_link.tx++;
          pw_bt_log(PW_BT_DIR_TX, echo);
        }
    }
  else
    {
      printf("[bt] echo skipped (文本特征未订阅)\n");
    }

  return len;
}

static void pw_text_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
  (void)attr;
  g_text_sub = (value & BT_GATT_CCC_NOTIFY) ? 1 : 0;
  g_link.subscribed = g_nfy_on || g_text_sub;
  printf("[bt] text ccc -> notify %s\n", g_text_sub ? "ON" : "OFF");
}

/* 属性表：0=service, 1=chrc decl, 2=chrc value(Sensor), 3=ccc, 4=chrc decl,
 *         5=chrc value(Command), 6=chrc decl, 7=chrc value(Text), 8=ccc
 * 新的文本特征**追加在表尾**，所以旧的句柄（0x0010/0x0011/0x0013）不变 ——
 * 手机上已经存过的句柄表、以及验收脚本里的结构断言都不会因此失效。 */

static struct bt_gatt_attr pw_attrs[] =
{
  BT_GATT_PRIMARY_SERVICE(&pw_svc_uuid.uuid),

  BT_GATT_CHARACTERISTIC(&pw_sensor_uuid.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ,
                         pw_read_sensor, NULL, &g_pkt),
  BT_GATT_CCC(pw_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

  BT_GATT_CHARACTERISTIC(&pw_cmd_uuid.uuid,
                         BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                         BT_GATT_PERM_WRITE,
                         NULL, pw_write_cmd, NULL),

  BT_GATT_CHARACTERISTIC(&pw_text_uuid.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
                         BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                         pw_text_read, pw_text_write, NULL),
  BT_GATT_CCC(pw_text_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};

static struct bt_gatt_service pw_svc = BT_GATT_SERVICE(pw_attrs);

/* ── 采样 + 通知 ────────────────────────────────────────────────────── */

/* 静止手表的三轴合矢量必然 ~1 g，用它当"这一读是不是垃圾"的判据：
 * pw_sensors_read_imu_oneshot() 只要 accel 非全 0 就接受，而它自己的注释就写了
 * "FIFO 刚重启时第一次常拿到全 0"，所以这里再加一道合矢量合理性检查。
 * （2026-09-18：服务端自检第一次跑出 |a|≈15.4 g，一度以为是传感器瞬态，
 *  实为本次重构把 IMU 读取整行删掉、读了未初始化的栈变量 —— 自检把这个自伤
 *  抓了出来。修好后实测 |a|≈1000 mg。） */
#define PW_ACC_MIN_MG   300
#define PW_ACC_MAX_MG   3000
#define PW_ACC_TRIES    4

/* strict 路径（GATT 读 / 自检）的总时间预算，单位 ms。
 * 为什么要有预算：这些回调跑在 Zephyr 系统工作队列线程上，而
 * pw_sensors_read_imu_oneshot() **一次**最坏就 ~160 ms（内部 3 次读 + START + 5 次读）。
 * 只按"次数"限（4 次）⇒ 最坏 ~640 ms 把 HCI 收包处理与命令 TX 全按住，
 * 手机连着时可能表现成卡顿甚至掉线。改成按**时间**限：
 *   - 最多 PW_ACC_TRIES 次；
 *   - 且只有"剩余预算还够再来一次"时才重试（PW_ACC_MIN_RETRY_MS）。
 * 拿不到就保留上一个好值（不更新 g_pkt），绝不为了一个读数把 BT 按住。
 * 注：单次 oneshot 自身的耗时无法从外面切断，所以下界就是一次调用的量级。 */
#define PW_SAMPLE_BUDGET_MS     200
#define PW_ACC_MIN_RETRY_MS     100

static int pw_imu_mag_ok(const struct pw_imu_s *imu)
{
  long ax = imu->ax;
  long ay = imu->ay;
  long az = imu->az;
  long mag2 = ax * ax + ay * ay + az * az;

  return mag2 >= (long)PW_ACC_MIN_MG * PW_ACC_MIN_MG &&
         mag2 <= (long)PW_ACC_MAX_MG * PW_ACC_MAX_MG;
}

/* 采样一次。
 *
 * `strict` 决定"要不要为了拿一个好样本反复重试" —— 这一点很关键，因为
 * **GATT 回调和周期通知都跑在 Zephyr 系统工作队列线程上**（bt_recv →
 * k_work_submit(&hdev->rx_work) → rx_work_handler，栈 4096 B，优先级 110）。
 * 而 pw_sensors_read_imu_oneshot() 内部最坏要走
 * 「3 次读 + START + 5 次读」≈160 ms。如果在这里再套一层重试（原来 6 次），
 * 最坏能把工作队列堵 ~1 秒 —— 会连带推迟 HCI 命令 TX 与所有收到的包的处理。
 *
 *   strict=1（GATT 读 / 自检）：用户主动触发，值得等 ⇒ 最多 PW_ACC_TRIES 次，
 *                               拿不到合理的就报 WARN；
 *   strict=0（500 ms 周期通知）：**只读一次**，样本不合理就**不更新** g_pkt
 *                               （保留上一个好值），绝不为了通知去堵队列。
 */
static void pw_sample_once(int strict)
{
  struct pw_imu_s imu;
  struct pw_light_s light;
  int tries = strict ? PW_ACC_TRIES : 1;
  int64_t deadline = k_uptime_get() + PW_SAMPLE_BUDGET_MS;
  int i;
  int ok = 0;

  memset(&imu, 0, sizeof(imu));

  for (i = 0; i < tries && !ok; i++)
    {
      if (pw_sensors_read_imu_oneshot(&imu) == 0 && pw_imu_mag_ok(&imu))
        {
          ok = 1;
          break;
        }

      /* 还有没有"再来一次"的预算？没有就立刻收手。 */
      if (!strict || k_uptime_get() + PW_ACC_MIN_RETRY_MS > deadline)
        {
          break;
        }

      usleep(20000);
    }

  if (ok)
    {
      g_pkt.ax = (int16_t)imu.ax;
      g_pkt.ay = (int16_t)imu.ay;
      g_pkt.az = (int16_t)imu.az;
      g_pkt.gx = (int16_t)(imu.gx / 10);
      g_pkt.gy = (int16_t)(imu.gy / 10);
      g_pkt.gz = (int16_t)(imu.gz / 10);
    }
  else if (strict)
    {
      printf("[bt] imu WARN %d 次都没有合矢量合理的样本\n", tries);
    }

  if (pw_sensors_read_light_oneshot(&light) == 0)
    {
      g_pkt.lux = (uint16_t)(light.lux < 0 ? 0 : light.lux);
    }
}

static void pw_sample_work_handler(struct k_work *work)
{
  (void)work;

  /* 顺便把**实时**连接参数缓存下来给界面用。
   * 为什么不在 UI 线程里查：那是 GUI 线程，不能去碰 BT 栈（跨线程）。
   * 为什么不在连接时查一次就算：实测 `le_param_updated` 回调在本链路
   * （中心设备是 BlueZ）**一次都没触发过**，只靠它界面就永远显示不出间隔。
   * 这里跑在 BT 工作队列上，每 500 ms 查一次，代价可以忽略。 */
  if (g_conn != NULL)
    {
      struct bt_conn_info ci;

      if (bt_conn_get_info(g_conn, &ci) == 0)
        {
          g_link.interval = ci.le.interval;
        }
    }

  /* 没有订阅者就不采样 —— 别为了没人看的数据每 500 ms 去开一次 /dev 传感器。
   * 有人读时会由 pw_read_sensor() 现采。 */
  if (g_nfy_on)
    {
      pw_sample_once(0);
    }

  if (g_nfy_on)
    {
      /* 诊断开关：为 1 时**每次**通知都打印 rc（排查"订阅了但收不到包"）；
       * 平时置 0，只在出错时打一行。
       * 2026-09-18：这个开关正是抓到"设备侧 rc=0、手机侧收不到"的功臣 ——
       * 顺着它打开 h4.c 的 PW_H4_TRACE，才在空口原文里看到通知帧的
       * value handle 是 0x0000（zblue gatt_notify_mc 漏 data.handle）。
       * 根因已修（见 docs/evidence/bt-gatt-handle-20260918/），开关归 0。 */
#define PW_BT_NOTIFY_TRACE 0
      static unsigned int nfy_tick;
      int rc = bt_gatt_notify(NULL, &pw_attrs[PW_ATTR_SENSOR_VALUE], &g_pkt,
                              sizeof(g_pkt));

      nfy_tick++;
#if PW_BT_NOTIFY_TRACE
      printf("[bt] notify #%u rc=%d len=%u\n", nfy_tick, rc,
             (unsigned)sizeof(g_pkt));
#else
      if (rc && rc != -ENOTCONN)
        {
          printf("[bt] notify #%u rc=%d\n", nfy_tick, rc);
        }
#endif
    }

  k_work_reschedule(&g_sample_work, K_MSEC(PW_SAMPLE_MS));
}

/* ── 连接事件 ──────────────────────────────────────────────────────────
 * B3 是**人工**验证（手机连），所以设备侧必须把"连上了/断了"打到串口上，
 * 否则只能靠手机屏幕，串口证据链是空的。 */

static void pw_connected(struct bt_conn *conn, uint8_t err)
{
  char addr[BT_ADDR_LE_STR_LEN];
  int rc;

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  printf("[bt] B3 connected: %s err=%u\n", addr, err);
  g_nfy_on = 0;
  g_text_sub = 0;

  if (err == 0)
    {
      g_conn = conn;
      g_link.connected = 1;
      g_link.subscribed = 0;
      g_link.mtu = bt_gatt_get_mtu(conn);
      strncpy(g_link.peer, addr, sizeof(g_link.peer) - 1);
      g_link.peer[sizeof(g_link.peer) - 1] = '\0';
      pw_bt_log(PW_BT_DIR_SYS, addr);
      printf("[bt] link up mtu=%u\n", (unsigned)g_link.mtu);

#if PW_BT_FAST_CONN
      /* 请求更短的连接间隔（结果由中心设备决定，见 le_param_updated） */
      rc = bt_conn_le_param_update(conn, &pw_conn_param);
      printf("[bt] conn param update req rc=%d (interval %u~%u x1.25ms)\n", rc,
             pw_conn_param.interval_min, pw_conn_param.interval_max);
#endif

#if PW_BT_MTU_EXCH
      /* 主动拉高 ATT MTU：默认 23 时一次写只能带 20 B 载荷 */
      rc = bt_gatt_exchange_mtu(conn, &pw_mtu_params);
      printf("[bt] exchange mtu rc=%d\n", rc);
#endif
    }
}

static void pw_disconnected(struct bt_conn *conn, uint8_t reason)
{
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  printf("[bt] B3 disconnected: %s reason=0x%02x\n", addr, reason);
  g_nfy_on = 0;
  g_text_sub = 0;

  g_conn = NULL;
  g_link.connected = 0;
  g_link.subscribed = 0;
  g_link.mtu = 0;
  g_link.interval = 0;
  g_link.peer[0] = '\0';
  g_text_pending = 0;
  pw_bt_log(PW_BT_DIR_SYS, "disconnected");

  /* 让手表回到"可被发现"，否则断了就再也连不上（要复位才行） */
  k_work_submit(&g_adv_work);
}

/* 协商后的连接参数：打印 + 记进链路状态（手表页面上要看得到） */
static void pw_le_param_updated(struct bt_conn *conn, uint16_t interval,
                                uint16_t latency, uint16_t timeout)
{
  (void)conn;
  g_link.interval = interval;
  printf("[bt] le param updated interval=%u (%.1f ms) latency=%u timeout=%u\n",
         (unsigned)interval, (double)interval * 1.25, (unsigned)latency,
         (unsigned)timeout);
  pw_bt_log(PW_BT_DIR_SYS, "conn param updated");
}

static void pw_security_changed(struct bt_conn *conn, bt_security_t level,
                                enum bt_security_err err)
{
  printf("[bt] security level=%d err=%d\n", (int)level, (int)err);
  pw_bt_log(PW_BT_DIR_SYS, "security changed");
}

static void pw_auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
  (void)conn;

  /* IO 能力由"注册了哪些回调"反推：只注册 passkey_display →
   * zblue 认为本机是 DisplayOnly，手机那侧出键盘、我们这侧出 6 位数。
   * 这样 AuthReq 里的 MITM 位才有人兑现，配对不会再被手机以
   * 0x03（Authentication Requirements）打回。 */

  printf("[bt] PASSKEY %06u —— 请在手机上输入这 6 位\n", passkey);
  pw_ui_show_passkey(passkey);
  pw_bt_log(PW_BT_DIR_SYS, "passkey on screen");
}

/* 配对回调：没有它，zblue 用默认 IO 能力（NoInputNoOutput）走 Just Works，
 * 但 CONFIG_BT_SMP_ENFORCE_MITM=y 又要求 MITM —— 二者矛盾时配对必然失败，
 * 手机侧的表现正是"显示已配对 1 秒，然后配对记录消失"。 */
static void pw_auth_cancel(struct bt_conn *conn)
{
  (void)conn;
  printf("[bt] SMP auth cancel\n");
}

static void pw_auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  printf("[bt] SMP pairing complete: %s bonded=%d\n", addr, (int)bonded);
  pw_bt_log(PW_BT_DIR_SYS, bonded ? "paired+bonded" : "paired");
}

static void pw_auth_pairing_failed(struct bt_conn *conn,
                                   enum bt_security_err reason)
{
  printf("[bt] SMP pairing FAILED reason=%d (%s)\n", (int)reason,
         bt_security_err_to_str(reason));
  pw_bt_log(PW_BT_DIR_SYS, "pairing failed");
}

/* zblue 把"配对完成/失败"放在 bt_conn_auth_info_cb 里，
 * bt_conn_auth_cb 只放 passkey/confirm/pincode/cancel。两个都要注册。 */
static const struct bt_conn_auth_cb pw_auth_cb =
{
  .cancel = pw_auth_cancel,
  .passkey_display = pw_auth_passkey_display,
};

/* 注意：这里**不能加 const**。bt_conn_auth_info_cb_register() 会把这个
 * 结构体挂进链表（sys_slist_append 写 cb->node），const 结构体落在 flash，
 * 写 flash 会直接把设备挂死（2026-11-04 实测：注册后整机停在 bt_enable 之后，
 * 无任何输出，等看门狗复位循环）。 */
/* 纯 Just Works 用的一组回调（不声明任何显示/输入能力）。
 * 用途：DSH 原体建议的分辨实验 J1 —— 冷启后第一次就做 JW 配对，
 * 若仍 Confirm Failed，说明不是 passkey/tk 的事。 */
static const struct bt_conn_auth_cb pw_auth_cb_jw =
{
  .cancel = pw_auth_cancel,
};

static struct bt_conn_auth_info_cb pw_auth_info_cb =
{
  .pairing_complete = pw_auth_pairing_complete,
  .pairing_failed = pw_auth_pairing_failed,
};

int pw_bt_set_io(int jw)
{
  const struct bt_conn_auth_cb *cb = jw ? &pw_auth_cb_jw : &pw_auth_cb;
  int rc;
  int lerc;

  lerc = bt_conn_le_auth_cb_register(cb);
  rc = bt_conn_auth_cb_register(cb);
  printf("[bt] IO=%s le_rc=%d rc=%d\n",
         jw ? "NoInputNoOutput(JW)" : "DisplayOnly(passkey)", lerc, rc);
  return rc;
}

static struct bt_conn_cb pw_conn_cb =
{
  .connected = pw_connected,
  .disconnected = pw_disconnected,
  .le_param_updated = pw_le_param_updated,
  .security_changed = pw_security_changed,
};

/* [T-0013] 临时名字：手机里存的旧记录（旧地址 + 旧密钥 + 旧名字）会让
 * "点配对"直接失败、连尝试都不发。改个名字等于给手机一台"新设备"，
 * 绕开它的缓存；配对打通后改回 "PhyWear"。 */

#define PW_BT_NAME "PhyWear"

/* ── 广播数据 ───────────────────────────────────────────────────────── */

static const struct bt_data pw_ad[] =
{
  /* [T-0013] 2026-11-04 15:2x 结论：去掉 NO_BREDR 反而更糟 —— 安卓于是把我们
   * 当双模设备、优先走经典蓝牙去连，而控制器对经典连接回
   * `evt 0x03 status=0x0d`（Connection Rejected due to Limited Resources），
   * 手机侧表现就是"点了配对没反应/直接失败"，而且一个 ACL 包都到不了应用层。
   * 恢复 NO_BREDR：广播只声明 LE，手机就只按 LE 连（配对要修的是 c1 的 rat，
   * 不是连接方式）。 */

  BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
  BT_DATA(BT_DATA_NAME_COMPLETE, PW_BT_NAME, sizeof(PW_BT_NAME) - 1),
};

static const struct bt_data pw_sd[] =
{
  BT_DATA_BYTES(BT_DATA_UUID128_ALL,
                0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0x90, 0x8f,
                0x5e, 0x4d, 0x2c, 0x1b, 0x00, 0xa0, 0xf1, 0xe0),
};

/* ── 服务端自检（B3 的可自动化部分）──────────────────────────────────
 * B3 的"空口"那一段必须有真实 BLE 中心设备（手机/蓝牙棒）才能验，
 * 但**服务端数据库 + 读/写回调**这一段不需要空口就能证明。
 * 本函数把这一段全部走一遍（用的就是手机连上后会走的那几个函数）：
 *   ① 逐条打印属性表（UUID / 权限 / read / write 回调是否存在）
 *   ② 采样一次真传感器，再走 bt_gatt_attr_read() 把 16 B 读出来并解析
 *   ③ 走一遍命令写回调（等价于手机往 …a002 写 "ping"）
 * 输出前缀统一 [bt] SELFTEST，便于脚本判定。 */

static int pw_btgatt_selftest(void)
{
  uint8_t buf[32];
  ssize_t n;
  size_t i;
  int fails = 0;

  printf("[bt] SELFTEST start (服务端自检，不含空口)\n");

  for (i = 0; i < ARRAY_SIZE(pw_attrs); i++)
    {
      const struct bt_gatt_attr *a = &pw_attrs[i];
      char u[BT_UUID_STR_LEN];

      bt_uuid_to_str(a->uuid, u, sizeof(u));
      printf("[bt] SELFTEST attr[%u] handle=0x%04x %s perm=0x%02x read=%c write=%c\n",
             (unsigned)i, a->handle, u, a->perm, a->read ? 'Y' : '-',
             a->write ? 'Y' : '-');
    }

  /* ①b 句柄结构自检。
   * 为什么只看 UUID/权限不够：`handle` 是 `bt_gatt_service_register()` **注册时**
   * 才写进属性的。若注册没真正生效，handle 会是 0 —— 那样手机来的 ATT 请求
   * 会全部找不到东西，而日志上"UUID/权限都对"看不出任何异常。
   * 另外通知能发出去的前提是：**CCC 紧跟在特征值后面**（标准四段布局
   * service-decl / chrc-decl / chrc-value / CCC），zblue 的 notify 正是靠
   * `bt_gatt_attr_value_handle()` 拿到值句柄、再据它找到对应的 CCC。
   * 这里把这两条都断言下来。 */
  {
    int all_nonzero = 1;

    for (i = 0; i < ARRAY_SIZE(pw_attrs); i++)
      {
        if (pw_attrs[i].handle == 0)
          {
            all_nonzero = 0;
          }
      }

    printf("[bt] SELFTEST handles: value=0x%04x ccc=0x%04x (ccc 应为 value+1)\n",
           pw_attrs[PW_ATTR_SENSOR_VALUE].handle,
           pw_attrs[PW_ATTR_SENSOR_VALUE + 1].handle);

    if (!all_nonzero)
      {
        printf("[bt] SELFTEST FAIL 有属性 handle=0 ⇒ 服务没真正注册\n");
        fails++;
      }

    if (pw_attrs[PW_ATTR_SENSOR_VALUE + 1].handle !=
        pw_attrs[PW_ATTR_SENSOR_VALUE].handle + 1)
      {
        printf("[bt] SELFTEST FAIL CCC 不紧跟特征值 ⇒ 通知可能发不出去\n");
        fails++;
      }
  }

  /* ② 读路径：先采样，再走真实 read helper。
   * 这里顺便把采样耗时**量出来**打印 —— 「预算 200 ms」不能只是注释里的一句话，
   * 得在真机日志里有数（这个回调与周期通知都在 BT 工作队列线程上）。 */
  {
    int64_t t0 = k_uptime_get();

    pw_sample_once(1);
    printf("[bt] SELFTEST sample took %d ms (budget %d ms)\n",
           (int)(k_uptime_get() - t0), PW_SAMPLE_BUDGET_MS);
  }

  {
    /* lux 单独报一次 rc：否则 g_pkt.lux==0 分不清"真的 0"还是"读失败" */
    struct pw_light_s l;

    memset(&l, 0, sizeof(l));
    int lrc = pw_sensors_read_light_oneshot(&l);

    printf("[bt] SELFTEST light rc=%d lux=%d ch0=%d ch1=%d\n", lrc, l.lux,
           l.ch0, l.ch1);
  }

  memset(buf, 0, sizeof(buf));
  n = bt_gatt_attr_read(NULL, &pw_attrs[PW_ATTR_SENSOR_VALUE], buf,
                        sizeof(buf), 0, &g_pkt, sizeof(g_pkt));
  printf("[bt] SELFTEST read sensor -> %d B (want %u)\n", (int)n,
         (unsigned)sizeof(g_pkt));
  if (n == (ssize_t)sizeof(g_pkt))
    {
      int16_t ax = (int16_t)(buf[0] | (buf[1] << 8));
      int16_t ay = (int16_t)(buf[2] | (buf[3] << 8));
      int16_t az = (int16_t)(buf[4] | (buf[5] << 8));
      uint16_t lux = (uint16_t)(buf[12] | (buf[13] << 8));
      long mag = 0;
      long t = (long)ax * ax + (long)ay * ay + (long)az * az;

      while (mag * mag < t)
        {
          mag++;
        }

      printf("[bt] SELFTEST decode ax=%d ay=%d az=%d mg |a|=%ld mg lux=%u\n",
             ax, ay, az, mag, (unsigned)lux);
      if (mag < 300 || mag > 3000)
        {
          printf("[bt] SELFTEST FAIL 合矢量 %ld mg 不合理（静止应 ~1000 mg）\n",
                 mag);
          fails++;
        }
    }
  else
    {
      fails++;
    }

  /* ③ 写路径：等价于手机往命令特征写一条命令。
   * 用 "selftest" 这个**专用**命令名，避免与人工手机写入（比如 "ping"）混淆 ——
   * B3 判定器就是靠区分这两者才不会误判"手机写进来了"。 */
  printf("[bt] SELFTEST write cmd \"selftest\" ->\n");
  n = pw_write_cmd(NULL, &pw_attrs[PW_ATTR_CMD_VALUE], "selftest", 8, 0, 0);
  printf("[bt] SELFTEST write ret=%d (want 8)\n", (int)n);
  if (n != 8 || g_cmd_count == 0)
    {
      fails++;
    }

  /* ③b 文本串口（…a003）：读一句状态 + 走一遍写回调。
   * 写回调除了记日志，还会尝试回 echo —— 没有订阅者时它走"跳过"分支，
   * 但**日志与计数必须照记**，否则页面在没人订阅时看着像"没收到"。 */
  {
    char sbuf[96];
    uint32_t rx0 = g_link.rx;

    memset(sbuf, 0, sizeof(sbuf));
    n = pw_text_read(NULL, &pw_attrs[PW_ATTR_TEXT_VALUE], sbuf,
                     sizeof(sbuf), 0);
    printf("[bt] SELFTEST text read -> %d B: '%s'\n", (int)n, sbuf);
    if (n <= 0)
      {
        fails++;
      }

    n = pw_text_write(NULL, &pw_attrs[PW_ATTR_TEXT_VALUE], "selftest", 8, 0, 0);
    if (n != 8 || g_link.rx != rx0 + 1)
      {
        printf("[bt] SELFTEST FAIL 文本写回调没记进日志/计数 (ret=%d rx=%u)\n",
               (int)n, (unsigned)g_link.rx);
        fails++;
      }

    /* 结构断言：文本特征的 CCC 也必须紧跟它的值句柄 */
    if (pw_attrs[PW_ATTR_TEXT_VALUE].handle == 0 ||
        pw_attrs[PW_ATTR_TEXT_VALUE + 1].handle !=
        pw_attrs[PW_ATTR_TEXT_VALUE].handle + 1)
      {
        printf("[bt] SELFTEST FAIL 文本特征句柄结构不对 (value=0x%04x ccc=0x%04x)\n",
               pw_attrs[PW_ATTR_TEXT_VALUE].handle,
               pw_attrs[PW_ATTR_TEXT_VALUE + 1].handle);
        fails++;
      }

    printf("[bt] SELFTEST text handles: value=0x%04x ccc=0x%04x cmd=0x%04x\n",
           pw_attrs[PW_ATTR_TEXT_VALUE].handle,
           pw_attrs[PW_ATTR_TEXT_VALUE + 1].handle,
           pw_attrs[PW_ATTR_CMD_VALUE].handle);
  }

  /* ④ 通知路径演练。
   * 为什么需要：这条链（CCC 订阅 → 周期采样 → bt_gatt_notify）**只在有订阅者时**
   * 才会跑，实验室里没有手机 ⇒ 它从来没被执行过；万一它坏在 B3 现场，表现就是
   * "手机订阅了但一个包都收不到"，而且没有任何日志。
   * 这里直接调一次 bt_gatt_notify()：
   *   - 没有订阅者时，zblue 的 gatt_notify_mc() 初值就是 -ENOTCONN（已核对源码
   *     host/gatt.c:3053），notify_cb 找不到 cfg->value == NOTIFY 的订阅者就不会改它；
   *   - 手机订阅之后，**同一处的 rc 应该变成 0** —— 这就是 B3 要看的那条判据。
   * 这一步证明"属性句柄能解析 + 调用链通"，并把期望值钉在日志里。 */
  {
    int nrc;

    g_nfy_on = 1;
    pw_sample_once(1);
    nrc = bt_gatt_notify(NULL, &pw_attrs[PW_ATTR_SENSOR_VALUE], &g_pkt,
                         sizeof(g_pkt));
    g_nfy_on = 0;

    printf("[bt] SELFTEST notify(无订阅者) rc=%d (期望 %d=-ENOTCONN；"
           "手机订阅后这里应变成 0)\n", nrc, -ENOTCONN);
    if (nrc != -ENOTCONN)
      {
        fails++;
      }
  }

  printf("[bt] SELFTEST %s (fails=%d)\n", fails == 0 ? "PASS" : "FAIL", fails);
  return fails;
}

/* ── 对外入口 ───────────────────────────────────────────────────────── */

/* 断线后重开广播。
 *
 * 为什么必须显式做（2026-09-18 用户报"手表一直显示已连接"时顺带查出来的）：
 *   本板 defconfig **没有开 `CONFIG_BT_ADV_PERSIST`**（`.config` 里 grep 计数为 0），
 *   而 zblue/Zephyr 的 `bt_le_adv_resume()` 在没开这个符号时是**空实现** ——
 *   也就是说：连接建起来之后一旦断开，设备**不会自己重新广播**，
 *   必须复位板子或重跑 `phywear cap bt` 才能再被搜到。
 *   对一个"拿来就能连"的手表来说这是硬伤（用户想重连时发现搜不到了）。
 *
 * 为什么用工作项而不是直接在 disconnected 回调里调 bt_le_adv_start：
 *   回调是在连接拆除的**过程中**执行的，那里立刻重开广播容易撞上还没释放完的
 *   广播实例；投到系统工作队列上等这一步走完再开更稳。
 * 为什么不用 CONFIG_BT_ADV_PERSIST：那要改 defconfig + resetconfig + 重编，
 *   而这里显式做一次还能**打一行 rc**，空口/串口上就有据可查。
 */
static void pw_adv_restart(struct k_work *work)
{
  int rc;

  (void)work;

  rc = bt_le_adv_start(BT_LE_ADV_CONN, pw_ad, ARRAY_SIZE(pw_ad), pw_sd,
                       ARRAY_SIZE(pw_sd));

  /* 实测（2026-09-18）：断开之后直接 start 会回 **-114 = -EALREADY** ——
   * 因为链路建立时控制器**自己**把 legacy 广播停了，而 host 侧的
   * `BT_ADV_ENABLED` 标志位**没有被清掉**（`adv.c` 在 BT_ADV_ENABLED 置位时
   * 直接 return -EALREADY）。这也正是内置的 `bt_le_adv_resume()` 永远不生效的
   * 原因：它的前置条件是 `BT_ADV_PERSIST && !BT_ADV_ENABLED`，第二个条件
   * 一直为假 ⇒ **断开后设备再也不会广播**（用户想重连时搜不到）。
   * 修法：遇到 EALREADY 先 stop（把标志位清干净，控制器会接受一次冗余的
   * "关闭广播"命令）再 start。两个 rc 都打出来，下次一眼能看出状态机对不对。 */
  if (rc == -EALREADY)
    {
      int src = bt_le_adv_stop();

      printf("[bt] adv busy(EALREADY), stop rc=%d, retry start\n", src);
      rc = bt_le_adv_start(BT_LE_ADV_CONN, pw_ad, ARRAY_SIZE(pw_ad), pw_sd,
                           ARRAY_SIZE(pw_sd));
    }

  printf("[bt] adv restart rc=%d %s\n", rc, rc == 0 ? "(ok)" : "(FAILED)");

  if (rc == 0)
    {
      pw_bt_log(PW_BT_DIR_SYS, "advertising again");
    }
}

int pw_btgatt_start(void)
{
  bt_addr_le_t addrs[1];
  size_t count = 1;
  char addr[18];
  int rc;

  memset(&g_pkt, 0, sizeof(g_pkt));
  memset(&g_link, 0, sizeof(g_link));
  pw_bt_log_reset();
  k_work_init(&g_text_work, pw_text_work_handler);
  k_work_init(&g_adv_work, pw_adv_restart);
  bt_gatt_cb_register(&pw_gatt_cb);

  rc = bt_gatt_service_register(&pw_svc);
  printf("[bt] B2 gatt register rc=%d %s\n", rc, rc == 0 ? "(ok)" : "(FAILED)");
  if (rc)
    {
      return rc;
    }

  k_work_init_delayable(&g_sample_work, pw_sample_work_handler);

  rc = bt_conn_cb_register(&pw_conn_cb);
  printf("[bt] B2 conn cb register rc=%d\n", rc);

  /* 先注册 auth 回调，再让中心设备发起配对 —— 否则配对失败时我们
   * 连原因都拿不到（2026-11-04：手机配对 1 秒后配对记录就消失）。 */
  /* [T-0013-A] 开机默认用 JW 那组回调（DSH 要求，避免依赖 NSH 手动切）
   * 实测 NSH 在 app 起来之后输入不可靠，`phywear btio jw` 常常没有回显。 */

  rc = bt_conn_auth_cb_register(&pw_auth_cb_jw);
  printf("[bt] B2 auth cb register(JW) rc=%d\n", rc);
  /* 关键：bt_conn_auth_cb_register() 只把回调存进 hdev->bt_auth（给 BR/EDR 用），
   * LE 的 SMP 读的是 hdev->le_auth —— 必须再调一次 LE 专用注册。
   * 只注册前者时，get_io_capa() 拿到 NULL → IO=NoInputNoOutput → AuthReq 不带
   * MITM 位（实测 0x21 而不是 0x25），手机按 Just Works 校验 confirm 必然失败。 */

  {
    int lerc = bt_conn_le_auth_cb_register(&pw_auth_cb_jw);

    printf("[bt] B2 LE auth cb register(JW) rc=%d\n", lerc);
  }

  rc = bt_conn_auth_info_cb_register(&pw_auth_info_cb);
  printf("[bt] B2 auth info cb register rc=%d\n", rc);

  /* ⚠️ 广播数据里我们写的是 "PhyWear"，但 **GAP 的 Device Name 特性**取的是
   * `CONFIG_BT_DEVICE_NAME`（本板默认 "Zephyr"）—— 两者不一致时：
   * 手机首次扫描看到 "PhyWear"，连上后读名字（或系统缓存）就变成 "Zephyr"。
   * **实测（2026-09-18，宿主 BlueZ）**：断开重连后设备名显示为 'Zephyr'，
   * 就是踩了这个坑。这里把 GAP 名字也设成 "PhyWear"。
   * （需要 CONFIG_BT_DEVICE_NAME_DYNAMIC=y，本板已开。） */
  rc = bt_set_name(PW_BT_NAME);
  printf("[bt] B2 set_name \"PhyWear\" rc=%d\n", rc);


  rc = bt_le_adv_start(BT_LE_ADV_CONN, pw_ad, ARRAY_SIZE(pw_ad), pw_sd,
                       ARRAY_SIZE(pw_sd));
  printf("[bt] B2 adv start rc=%d %s\n", rc, rc == 0 ? "(ok)" : "(FAILED)");
  if (rc)
    {
      return rc;
    }

  bt_id_get(addrs, &count);
  if (count > 0)
    {
      snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
               addrs[0].a.val[5], addrs[0].a.val[4], addrs[0].a.val[3],
               addrs[0].a.val[2], addrs[0].a.val[1], addrs[0].a.val[0]);
    }
  else
    {
      snprintf(addr, sizeof(addr), "??");
    }

  /* 别把 identity 地址当成"广播地址"：实测 H4 原文里
   * LE Set Extended Advertising Parameters 的 own_addr_type = 0x01(RANDOM)，
   * 且先发了 LE Set Advertising Set Random Address —— 手机上看到的是**随机地址**，
   * 按 MAC 找会找不到，必须按名字找。（所以这里同时打印 identity 供对照。） */
  printf("[bt] B2 READY name=PhyWear identity=%s\n", addr);
  printf("[bt] B2 NOTE 广播用随机地址(EXT_ADV+PRIVACY)：手机请按名字找，别按 MAC\n");
  printf("[bt] B2 svc e0f1a000-1b2c-4d5e-8f90-a1b2c3d4e5f6\n");
  printf("[bt] B2   sensor e0f1a001 READ|NOTIFY (16B) / cmd e0f1a002 WRITE\n");

  k_work_reschedule(&g_sample_work, K_MSEC(PW_SAMPLE_MS));

  pw_btgatt_selftest();
  return 0;
}
