/* pw_bnep_frame.h — BNEP (Bluetooth Network Encapsulation Protocol) 帧层
 *
 * 纯逻辑：只做「字节 ↔ 结构」的编解码，不含任何蓝牙栈调用。
 * 供 apps/examples/phywear/pw_bnep.c 使用；也可在 Linux 上用 gcc 跑单测
 * （见同目录 test_bnep_frame.c）。
 *
 * 依据：Bluetooth Core / IEEE 802.15.1 BNEP 章节。
 *  - 第一个字节是包类型；控制包的类型字节高 3 位是 UUID 长度码。
 *  - 本实现默认只用 GENERAL_ETHERNET（最简单、无 802.1Q 分支）。
 */

#ifndef __PW_BNEP_FRAME_H
#define __PW_BNEP_FRAME_H

#include <stdint.h>

/* ---- 包类型（buf[0] 的低 5 位 / 数据包整字节） ---- */
#define PW_BNEP_GENERAL_ETHERNET    0x00
#define PW_BNEP_CONTROL             0x01
#define PW_BNEP_COMPRESSED          0x02
#define PW_BNEP_COMPRESSED_SRC_ONLY 0x03
#define PW_BNEP_COMPRESSED_DST_ONLY 0x04

/* ---- 控制类型（控制包 buf[0] 的低 5 位之后那一字节） ---- */
#define PW_BNEP_CTL_SETUP_REQ       0x00
#define PW_BNEP_CTL_SETUP_RSP       0x01
#define PW_BNEP_CTL_FILTER_SET      0x02
#define PW_BNEP_CTL_FILTER_RSP      0x03
#define PW_BNEP_CTL_MFILTER_SET     0x04
#define PW_BNEP_CTL_MFILTER_RSP     0x05

/* UUID 长度码（控制类型字节的高 3 位） */
#define PW_BNEP_UUID_NONE           0
#define PW_BNEP_UUID16              1
#define PW_BNEP_UUID32              2
#define PW_BNEP_UUID128             3

/* 服务 UUID（16 位） */
#define PW_BNEP_UUID_PANU           0x1115
#define PW_BNEP_UUID_NAP            0x1116

/* 以太类型 */
#define PW_BNEP_ETH_IPV4            0x0800
#define PW_BNEP_ETH_ARP             0x0806

#define PW_BNEP_ETH_ADDR_LEN        6
#define PW_BNEP_VLAN_LEN            4
/* GENERAL_ETHERNET: 1 + 6 + 6 + 2 */
#define PW_BNEP_HDR_LEN             (1 + PW_BNEP_ETH_ADDR_LEN * 2 + 2)

/* 解析结果 */
struct pw_bnep_hdr
{
  uint8_t         type;     /* PW_BNEP_* 包类型 */
  uint8_t         ctl;      /* 控制包：控制类型（不含 UUID 码） */
  uint8_t         uuid_code;/* 控制包：UUID 长度码 */
  const uint8_t  *payload;  /* 控制包载荷首字节（数据包为 NULL） */
  uint32_t        paylen;
  const uint8_t  *eth;      /* 数据包：以太帧首字节（以 dest 开头） */
  uint32_t        ethlen;
};

/* 网络类型过滤区间（过滤集合里的一项） */
struct pw_bnep_ftype
{
  uint16_t start;
  uint16_t end;
};

/* 编码 / 解析；返回值 >=0 为写入或消耗的字节数，<0 为 -EINVAL 等错误码 */
int32_t pw_bnep_parse(const uint8_t *buf, uint32_t len, struct pw_bnep_hdr *out);

/* 控制包：SETUP_CONNECTION_REQ（PANU→NAP：dst=NAP，src=PANU） */
int32_t pw_bnep_build_setup_req(uint8_t *buf, uint32_t cap,
                               uint16_t src_uuid, uint16_t dst_uuid);

/* 控制包：SETUP_CONNECTION_RSP 里取 2 字节响应码（0 = 成功） */
int32_t pw_bnep_parse_setup_rsp(const uint8_t *buf, uint32_t len, uint16_t *code);

/* 控制包：FILTER_NET_TYPE_SET */
int32_t pw_bnep_build_filter_set(uint8_t *buf, uint32_t cap,
                                 const struct pw_bnep_ftype *ranges, uint8_t nranges);

/* 控制包：FILTER_NET_TYPE_RSP 里取 2 字节响应码 */
int32_t pw_bnep_parse_filter_rsp(const uint8_t *buf, uint32_t len, uint16_t *code);

/* 数据包：把一个以太帧封成 GENERAL_ETHERNET（buf 需 cap >= len + 1） */
int32_t pw_bnep_build_data(uint8_t *buf, uint32_t cap, const uint8_t *eth, uint32_t ethlen);

#endif /* __PW_BNEP_FRAME_H */
