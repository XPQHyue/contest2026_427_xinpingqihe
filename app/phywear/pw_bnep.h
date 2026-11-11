/****************************************************************************
 * apps/examples/phywear/pw_bnep.h
 *
 * 蓝牙 PAN（PANU 角色）—— 本项目的上网通道之一。
 *
 * 由来（2026-11-04）：小智（78/xiaozhi-sf32）在同一块 SF32 板子上能用 PAN，
 * 我们不行。取证结论：同板不同栈 —— 小智是 RT-Thread + SiFli BTS2 栈（自带
 * BNEP/PAN profile），我们是 openvela + zblue（BR/EDR 有、BNEP 没有），
 * 小米 bluelet 栈虽有 Kconfig 但没有源码也没有预编译件。
 * 所以这一层由我们自己补：SDP 查 NAP → L2CAP BR 连 PSM 0x000F → BNEP 握手
 * → 以太帧交给 NuttX 的 TAP(/dev/tun)。
 *
 * 纯帧编解码在 pw_bnep_frame.c（已在 Linux 上单测 41/41），本文件只做粘合。
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_BNEP_H
#define __APPS_EXAMPLES_PHYWEAR_PW_BNEP_H

#include <stdbool.h>
#include <stddef.h>

/* 开始：peer 为手机 BR/EDR 地址（"AA:BB:CC:DD:EE:FF"），手机侧需已开热点。
 * 返回 0 表示已发起流程（异步）；真正的"通了"看 pw_pan_is_up()。 */

int  pw_pan_start(const char *peer_mac);

/* 从 BLE 文本通道触发：有 ACL 就直接开 PAN；没有就先建 BR/EDR ACL，
 * 等 pw_connected() 里接力。返回 0=已发起，-EAGAIN=已发起建链，其它=失败。 */

int  pw_pan_connect(const char *peer_mac);

/* 已有 ACL 时直接用连接对象起动（BR 地址字符串在 zblue 里可能不可靠，
 * 这条路径不经过字符串解析）。 */

int  pw_pan_start_conn(struct bt_conn *conn);

/* 启一个 2 s 轮询线程：只要存在 BR/EDR 连接就自动起 PANU。幂等。 */

void pw_pan_poll_start(void);

/* 安全等级达标回调里调用：等 3 s 再起 PANU（事件驱动，代替轮询）。 */

void pw_pan_start_on_security(struct bt_conn *conn);
int  pw_pan_stop(void);
bool pw_pan_is_up(void);

/* 给 `phywear pan status` 用：把当前状态写进 buf，返回写入长度 */

int  pw_pan_status(char *buf, size_t len);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_BNEP_H */
