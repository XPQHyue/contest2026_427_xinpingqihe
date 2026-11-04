/****************************************************************************
 * apps/examples/phywear/pw_btgatt.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 蓝牙链路状态 + 手表版文本串口（PhyWear）—— 给 UI 用的只读视图。
 *
 * 为什么单独抽一个头：
 *   B3 打通之后，界面上要显示"连没连上"（主界面灰/蓝点）以及一个
 *   「蓝牙」页（状态 + 收发文本日志）。这两处都需要读 BT 侧的状态，
 *   而 BT 侧的状态是**另一个线程**在改 —— 这层跨线程约定必须写在一个
 *   地方，不能散落在 UI 代码里各写一份"我觉得它是这么变的"。
 *
 * ── 跨线程约定（唯一重要的事）────────────────────────────────────────
 *   写入方：zblue 回调（连接/断开/CCC/写特征），跑在 **Zephyr 系统工作队列**
 *           线程上；
 *   读取方：LVGL 的 lv_timer 回调，跑在 **phywear 主线程**上。
 *
 *   所以：
 *     ① BT 回调里**绝不允许**调用任何 LVGL 函数 —— 那是跨线程碰渲染器，
 *        必崩，而且崩在连接/断开那一刻（最像"蓝牙自己坏了"，最难查）；
 *     ② UI 也不允许直接改这些字段，只能读；
 *     ③ 两边都不加锁：字段都是单字宽读写（见下），日志用 `seq` 标定有效性。
 *        最坏情况是 UI 读到一行"写到一半"的文本，250 ms 后的下一次刷新会
 *        自我修正 —— 为了这个去在 HCI 回调里拿互斥量是不划算的
 *        （会在射频事件路径上引入阻塞，可能丢包）。
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_BTGATT_H
#define __APPS_EXAMPLES_PHYWEAR_PW_BTGATT_H

#include <stdint.h>

/* 日志行方向 */
#define PW_BT_DIR_RX   0   /* 手机 → 手表 */
#define PW_BT_DIR_TX   1   /* 手表 → 手机 */
#define PW_BT_DIR_SYS  2   /* 连接/断开/订阅等状态变化 */

#define PW_BT_LOG_LINES  10
#define PW_BT_TEXT_MAX   35   /* 文本特征单条最大字节数（不含结尾 NUL） */
#define PW_BT_PEER_MAX   40   /* "XX:XX:XX:XX:XX:XX (public)" + 余量 */

struct pw_bt_logline_s
{
  volatile uint8_t dir;             /* PW_BT_DIR_* */
  char             text[PW_BT_TEXT_MAX + 1];
};

struct pw_bt_link_s
{
  volatile uint8_t  connected;      /* 1 = 有中心设备连着 */
  volatile uint8_t  subscribed;     /* 1 = 任一特征被订阅了通知 */
  volatile uint16_t mtu;            /* **协商后**的 ATT MTU（由 att_mtu_updated 刷新；
                                      * 不是连接瞬间那个默认值 23）；断开为 0 */
  volatile uint16_t interval;       /* 协商后的连接间隔，单位 1.25 ms（断开为 0） */
  volatile uint32_t rx;             /* 手机写进来的文本条数 */
  volatile uint32_t tx;             /* 成功 notify 出去的文本条数 */
  volatile uint32_t echo;           /* 成功回给手机的 echo 条数 */
  volatile uint32_t seq;            /* 日志已写入的行数（单调递增，满环后继续加） */
  char              peer[PW_BT_PEER_MAX];
  struct pw_bt_logline_s log[PW_BT_LOG_LINES];
};

/* 取只读视图。返回的指针在整个进程生命周期内有效（静态存储）。 */
FAR const struct pw_bt_link_s *pw_bt_link(void);

/* 手表 → 手机：把一段文本经文本特征 notify 出去。
 *
 * 返回值：
 *    0        已排入发送队列（真正发出在 Zephyr 工作队列线程上）
 *   -ENOTCONN 当前没有中心设备连接
 *   -EBUSY    上一条还没发完（按钮连点时的正常拒绝，不是错误）
 *   -EINVAL   空串或超长
 *
 * 为什么不在调用点直接 bt_gatt_notify：调用方是 **GUI 线程**，
 * 而 bt_gatt_notify 会走 HCI 命令/ACL 发送路径，必须在 BT 栈自己的工作
 * 队列上下文里跑。所以这里只投递一个 k_work，让它在对的线程上执行。
 */
int pw_bt_send_text(FAR const char *text);

/* 运行时切换 LE 配对的 IO 能力：jw=1 纯 Just Works；jw=0 屏上显示 passkey。
 * 仅用于配对问题定位（DSH 建议的 J1/J2 对照实验）。 */
int pw_bt_set_io(int jw);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_BTGATT_H */
