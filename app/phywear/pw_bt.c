/****************************************************************************
 * apps/examples/phywear/pw_bt.c
 *
 * 蓝牙阶段 B-1：起 zblue host 栈。判据：打印 rc=0 ⇒ host 栈已初始化。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/bluetooth/classic/classic.h>

#include "pw_bt.h"

/* 经典可连接/可被发现：做 PAN(BNEP) 必须打开。
 * 历史：T-0013 排查期曾临时置 0（误以为"控制器拒绝经典"），后来确认控制器接受
 * Write Scan Enable（0x0c1a status=0x00），真正的问题是连接池只有 1 个槽
 * （CONFIG_BT_MAX_CONN）+ 可发现性没生效，见 BOARD §33/§34。 */
#ifndef PW_BR_CONNECTABLE
#  define PW_BR_CONNECTABLE  1
#endif
#ifndef PW_BR_DISCOVERABLE
#  define PW_BR_DISCOVERABLE 1
#endif
#include "phywear_ui.h"

int pw_btgatt_start(void);

/* 栈只起一次。
 *
 * 为什么必须幂等（2026-09-18 加）：自从「蓝牙」页会自己确保栈已启动之后，
 * pw_bt_init() 可能被两条路各调一次 —— NSH 的 `phywear cap bt` 一次、
 * 用户点开蓝牙页再一次。第二次 bt_enable() 会回 -EALREADY，而后面那次
 * pw_btgatt_start() 会把同一个 service 再注册一遍（属性表被重新赋 handle，
 * 广播也会重复开），属于"看着能跑、状态已经乱了"的那类问题。
 * 所以这里显式记住"是不是我起的"，第二次直接返回成功。 */

static int g_bt_up;

int pw_bt_init(void)
{
  struct timespec t0;
  struct timespec t1;
  long ms;
  int rc;

  if (g_bt_up)
    {
      printf("[bt] bt_enable skipped (host stack already up)\n");
      return 0;
    }

  /* 单调时钟量 bt_enable 的**耗时**：这是区分「命令超时（HCI_CMD_TIMEOUT = 10 s）」
   * 与「控制器立刻回了非 0 status / 发送路径当场失败」的第一手判据 ——
   * 两者都会让 rc 变成负数，但含义完全不同（B1 根因就是靠它锁定"4 ms 快失败"）。 */

  clock_gettime(CLOCK_MONOTONIC, &t0);
  printf("[bt] calling bt_enable(NULL) ...\n");
  rc = bt_enable(NULL);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  ms = (long)(t1.tv_sec - t0.tv_sec) * 1000L +
       (long)(t1.tv_nsec - t0.tv_nsec) / 1000000L;

  printf("[bt] bt_enable rc=%d %s elapsed=%ldms\n", rc,
         rc == 0 ? "(host stack up)" : "(FAILED)", ms);

  /* B1 过了才做 B2：注册 GATT 服务 + 开可被发现广播。
   * 失败只打印，不影响 B1 的判据。 */

  if (rc == 0)
    {
      int brc;
      int irc;

      g_bt_up = 1;
      pw_btgatt_start();

      /* [AGENT-VM] 2026-11-04 T-0009：经典蓝牙要"可连接 + 可被发现"。
       *
       * 驱动层（`sf32lb52_bth4.c`）原来把 WRITE_SCAN_ENABLE 吞掉并回成功，
       * 于是 `bt_br_set_connectable()` 看起来成功、控制器其实没开 page scan
       * —— 手机搜不到本机。P1–P6 直发探针证明控制器支持这条命令（status
       * 0x00），已把该命令从 emulate 列表摘掉；这里再显式打开：
       *   connectable  → page scan（手机能连）
       *   discoverable → page + inquiry scan（手机能搜到）
       * 顺序不能反：discoverable 要求 PSCAN 已置位。
       */

      brc = bt_br_set_connectable(PW_BR_CONNECTABLE);
      irc = bt_br_set_discoverable(PW_BR_DISCOVERABLE);
      printf("[bt] BR/EDR connectable=%d rc=%d discoverable=%d rc=%d\n",
             PW_BR_CONNECTABLE, brc, PW_BR_DISCOVERABLE, irc);
    }

  return rc;
}

int pw_bt_is_up(void)
{
  return g_bt_up;
}

/* ── 异步起栈（2026-09-18 深夜加）─────────────────────────────────────
 *
 * `bt_enable()` 实测 ~1.2 s（每次开机日志都是 `elapsed=1228ms`），而主页的
 * 蓝牙标识与蓝牙页都在 **LVGL 主线程**上画 —— 同步调用就是"点一下冻一秒"。
 * 所以把启动挪到一个后台线程，界面继续刷。
 *
 * 两个刻意的选择：
 *  ① 优先级写 **裸值 90**（不用 K_PRIO_PREEMPT）。本端口
 *     `K_PRIO_PREEMPT(x) = CONFIG_NUM_COOP_PRIORITIES(100) + x`，而 phywear
 *     主线程优先级就是 100；NuttX 又是"数值越大越紧急" ⇒ 用宏只会得到
 *     >=100 的线程，照样抢界面。90 < 100 才真的让界面先跑。
 *  ② 栈从**堆**上要（8 KB）。静态 `K_THREAD_STACK_DEFINE` 是无条件吃 SRAM 的，
 *     而本板 SRAM 已 92%（见 pw_btppp.c 里同一套理由）。
 *
 * 线程是 pthread（`k_thread_create` → `pthread_create`）⇒ 挂在 phywear 的
 * task_group 下，`bt_enable()` 里 open 的 H4 fd 与它创建的接收线程同组 ——
 * 这正是 B1 根因（fd 表按 task_group）要求的行为。 */

#define PW_BT_INIT_PRIO   90
#define PW_BT_INIT_STACK  8192

static volatile int g_bt_starting;
static void        *g_bt_init_stack;
static struct k_thread g_bt_init_thread;

static void pw_bt_init_task(void *a, void *b, void *c)
{
  uint32_t f0;

  (void)a;
  (void)b;
  (void)c;

  /* 取证：起栈这一秒多里，界面线程推了多少帧。
   * 同步实现（改动前）这个数**恒为 0** —— 因为调用者就是界面线程本身；
   * 异步之后界面继续跑，这个数应该是个正数（≈1.2 s × 当页 fps）。 */
  f0 = pw_ui_flush_count();

  pw_bt_init();

  printf("[bt] 起栈期间界面仍在推帧：%lu 帧（同步实现下这里只会是 0）\n",
         (unsigned long)(pw_ui_flush_count() - f0));

  g_bt_starting = 0;              /* 成功则 g_bt_up=1；失败则允许下次重试 */
}

int pw_bt_init_async(void)
{
  uintptr_t p;

  if (g_bt_up || g_bt_starting)
    {
      return 0;                   /* 已起来 / 正在起：不重复 */
    }

  if (g_bt_init_stack == NULL)
    {
      p = (uintptr_t)malloc(PW_BT_INIT_STACK + 32);
      if (p == 0)
        {
          printf("[bt] 起栈线程 malloc(%d) 失败 errno=%d\n",
                 PW_BT_INIT_STACK, errno);
          return -ENOMEM;
        }

      g_bt_init_stack = (void *)((p + 31u) & ~(uintptr_t)31u);
    }

  g_bt_starting   = 1;
  g_bt_init_thread.init_data = NULL;

  k_thread_create(&g_bt_init_thread, g_bt_init_stack, PW_BT_INIT_STACK,
                  pw_bt_init_task, NULL, NULL, NULL,
                  PW_BT_INIT_PRIO, 0, K_NO_WAIT);
  k_thread_name_set(&g_bt_init_thread, "pw_btinit");

  printf("[bt] 起栈线程已投递（prio %d，栈 %d B，来自堆）—— 界面不阻塞\n",
         PW_BT_INIT_PRIO, PW_BT_INIT_STACK);
  return 0;
}

int pw_bt_is_starting(void)
{
  return g_bt_starting;
}
