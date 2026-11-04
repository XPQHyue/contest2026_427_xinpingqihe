/****************************************************************************
 * apps/examples/phywear/pw_btprobe.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 蓝牙阶段 A 探针（异步，不阻塞 GUI）。详见 pw_btprobe.c 顶部注释。
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_BTPROBE_H
#define __APPS_EXAMPLES_PHYWEAR_BTPROBE_H

/* Phase 0 探针：对 /dev/ttyHCI0 依次发 HCI_Reset / Read Local Version /
 * Read BD_ADDR / Read Local Supported Features / Read Local Supported Commands，
 * 打印控制器自报能力（BR/EDR 判据见 .c 顶部注释）。
 *
 * 0 = 取到特性页；-1 = 端口打不开；-2 = Reset 无 Command Complete；
 * -3 = 保留；-4 = Read Local Supported Features 失败（无法判断）。 */

int pw_bt_probe(void);

/* P1–P6：BR/EDR 配置命令直发探针（绕过 sf32lb52_bth4.c 的 emulate 层）。
 * 判据：status 0x00 = 控制器支持该命令；0x01 = 参数/状态被拒；0x0C = 未实现。
 * 0 = 跑完；-1 = 端口打不开。 */

int pw_bt_bredr_probe(void);

/* P7–P9：只读回读 scan_enable / class_of_device / local_name，不发 Reset。 */

int pw_bt_bredr_state(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_BTPROBE_H */
