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
int  pw_pan_stop(void);
bool pw_pan_is_up(void);

/* 给 `phywear pan status` 用：把当前状态写进 buf，返回写入长度 */

int  pw_pan_status(char *buf, size_t len);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_BNEP_H */
