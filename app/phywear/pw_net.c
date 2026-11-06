/****************************************************************************
 * apps/examples/phywear/pw_net.c
 *
 * 任务 3 的替代通路：USB CDC-ACM + SLIP。
 *
 * 为什么需要它（背景必须说清，否则会误以为在绕开问题）：
 *   本板（黄山派 SF32LB52）**没有无线网卡** —— 运行时 `ifconfig` 直接
 *   `ifconfig: open failed: 2`（ENOENT），`/dev` 里没有任何 wlan/eth，
 *   defconfig 也没有 `CONFIG_WIFI`/`CONFIG_DRIVERS_IEEE80211`。
 *   所以 ai_agent 的 `set_wifi <ssid> <pass>`（内部走 `ifup wlan0` + `wapi`）
 *   在本板上**物理不可达**，不是软件没写完。
 *
 *   但"让手表拿到 IP、把测量数据发到宿主"这件事本身**可以在本板实现**，
 *   换一条不需要网卡的链路即可：
 *       SoC USB 设备口（`PAD_PA35/PA36 = USB_DP/DM`，板级 pinmux 已配）
 *         → CDC-ACM（`/dev/ttyACM0`，板级已 `cdcacm_initialize(0, NULL)`）
 *         → **SLIP**（`nuttx/drivers/net/slip.c`，只差 `CONFIG_NET_SLIP=y`）
 *         → 宿主 `slattach` 起对端 → 板子拿到 IP → TCP/UDP 通
 *   这一条**只需要一根 USB 数据线**，不需要 WiFi 硬件。
 *
 * 本文件只做板子这一侧：把 SLIP 网卡注册起来并打印 rc。
 * 之后用 NSH 的 `ifconfig sl0 <ip>` 可以立刻验证 IP 层是通的
 * （没有对端也能设地址/看 netdev），最后那一跳（宿主 SLIP 对端 + DHCP）
 * 需要插上 USB 线才能验。
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>

#include <nuttx/net/slip.h>

#define PW_NET_SLIP_DEV  "/dev/ttyACM0"

int pw_net_up(void)
{
  int rc;

  printf("[net] 本板无无线网卡（ifconfig: open failed: 2 = ENOENT）⇒ set_wifi 不可达\n");
  printf("[net] 改走 USB CDC-ACM + SLIP：slip_initialize(0, %s)\n", PW_NET_SLIP_DEV);

#ifdef CONFIG_NET_SLIP
  rc = slip_initialize(0, PW_NET_SLIP_DEV);
#else
  printf("[pw_net] CONFIG_NET_SLIP 未启用，跳过\n");
  rc = -ENOSYS;
#endif
  printf("[net] slip_initialize rc=%d %s\n", rc, rc == 0 ? "(sl0 已注册)" : "(FAILED)");

  if (rc == 0)
    {
      printf("[net] 下一步（NSH）：ifconfig sl0 192.168.7.2  →  ifconfig 应显示 sl0 + 该地址\n");
      printf("[net] 端到端（需插 USB 线）：宿主 slattach/路由 + 板子 renew sl0 拿 DHCP\n");
    }

  return rc;
}
