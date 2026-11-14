/* pw_bnep_frame.c — 见 pw_bnep_frame.h 顶部说明。无动态分配、无全局状态。 */

#include "pw_bnep_frame.h"

#include <string.h>

#ifndef EINVAL
#  define EINVAL 22
#endif
#ifndef EOPNOTSUPP
#  define EOPNOTSUPP 95
#endif

static uint16_t bnep_be16(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void bnep_put_be16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xff);
}

int32_t pw_bnep_parse(const uint8_t *buf, uint32_t len, struct pw_bnep_hdr *out)
{
  uint8_t t;

  if (buf == NULL || out == NULL || len < 1)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));
  t = buf[0];

  /* 控制包：低 5 位恒为 0x01，高 3 位是 UUID 长度码 */
  if ((t & 0x1f) == PW_BNEP_CONTROL)
    {
      if (len < 2)
        {
          return -EINVAL;
        }

      out->type      = PW_BNEP_CONTROL;
      out->uuid_code = (uint8_t)(t >> 5);
      out->ctl       = buf[1];
      out->payload   = buf + 2;
      out->paylen    = len - 2;
      return (int32_t)len;
    }

  /* 数据包：高 3 位必须为 0 */
  if (t > PW_BNEP_COMPRESSED_DST_ONLY)
    {
      return -EINVAL;
    }

  out->type = t;

  switch (t)
    {
      case PW_BNEP_GENERAL_ETHERNET:
        if (len < PW_BNEP_HDR_LEN)
          {
            return -EINVAL;
          }

        out->eth    = buf + 1;
        out->ethlen = len - 1;
        return (int32_t)len;

      case PW_BNEP_COMPRESSED:
        /* 1 + dst(6) + src(6) + vlan(4) + ethertype(2) */
        if (len < (uint32_t)(PW_BNEP_HDR_LEN + PW_BNEP_VLAN_LEN))
          {
            return -EINVAL;
          }

        return -EOPNOTSUPP;

      case PW_BNEP_COMPRESSED_SRC_ONLY:
        if (len < (uint32_t)(PW_BNEP_HDR_LEN + PW_BNEP_VLAN_LEN -
                             PW_BNEP_ETH_ADDR_LEN))
          {
            return -EINVAL;
          }

        return -EOPNOTSUPP;

      case PW_BNEP_COMPRESSED_DST_ONLY:
        if (len < (uint32_t)(PW_BNEP_HDR_LEN + PW_BNEP_VLAN_LEN -
                             PW_BNEP_ETH_ADDR_LEN))
          {
            return -EINVAL;
          }

        return -EOPNOTSUPP;

      default:
        return -EINVAL;
    }
}

int32_t pw_bnep_build_setup_req(uint8_t *buf, uint32_t cap,
                               uint16_t src_uuid, uint16_t dst_uuid)
{
  if (buf == NULL || cap < 6)
    {
      return -EINVAL;
    }

  /* 类型字节：CONTROL(0x01) | uuid16 码(1) << 5 = 0x21 */
  buf[0] = (uint8_t)(PW_BNEP_CONTROL | (PW_BNEP_UUID16 << 5));
  buf[1] = PW_BNEP_CTL_SETUP_REQ;
  bnep_put_be16(&buf[2], dst_uuid);   /* 先目的（NAP） */
  bnep_put_be16(&buf[4], src_uuid);   /* 再源（PANU） */
  return 6;
}

int32_t pw_bnep_parse_setup_rsp(const uint8_t *buf, uint32_t len, uint16_t *code)
{
  uint32_t usize;
  uint8_t  ucode;

  if (buf == NULL || code == NULL || len < 4)
    {
      return -EINVAL;
    }

  if ((buf[0] & 0x1f) != PW_BNEP_CONTROL || buf[1] != PW_BNEP_CTL_SETUP_RSP)
    {
      return -EINVAL;
    }

  /* [关键修复·2026-11-12] SETUP_CONNECTION_RESPONSE 的格式是
   *   <type> <ctl> <dst UUID> <src UUID> <result:2>
   * 结果码在最后，不是紧跟 ctl。UUID 长度由 type 高 3 位决定：
   * 1 -> 2 字节，2 -> 4 字节，4 -> 16 字节。
   * 之前按 <ctl><result> 解析，把 dst UUID 的 0x1116 当成了结果码。 */

  ucode = (uint8_t)(buf[0] >> 5);
  usize = (ucode == 1) ? 2u : (ucode == 2) ? 4u : (ucode == 4) ? 16u : 0u;
  if (usize == 0u || len < (2u + 2u * usize + 2u))
    {
      return -EINVAL;
    }

  *code = bnep_be16(&buf[2u + 2u * usize]);
  return 0;
}

int32_t pw_bnep_build_filter_set(uint8_t *buf, uint32_t cap,
                                 const struct pw_bnep_ftype *ranges, uint8_t nranges)
{
  uint32_t need = 4u + (uint32_t)nranges * 4u;
  uint8_t  i;

  if (buf == NULL || ranges == NULL || nranges == 0 || cap < need)
    {
      return -EINVAL;
    }

  buf[0] = PW_BNEP_CONTROL;              /* 不带 UUID */
  buf[1] = PW_BNEP_CTL_FILTER_SET;

  /* 2 字节「过滤器列表长度」= 字节数（4 * nranges），不是条目个数。
   * 依据 AOSP Fluoride bnep_utils.cc:264-272（UINT16_TO_BE_STREAM(4 * nfilters)），
   * 接收侧 :786-802 也是先读 len 再跳 len 字节。缺它 → 对端判 bad length。 */
  bnep_put_be16(&buf[2], (uint16_t)(4u * (uint32_t)nranges));

  for (i = 0; i < nranges; i++)
    {
      bnep_put_be16(&buf[4 + i * 4], ranges[i].start);
      bnep_put_be16(&buf[4 + i * 4 + 2], ranges[i].end);
    }

  return (int32_t)need;
}

int32_t pw_bnep_parse_filter_rsp(const uint8_t *buf, uint32_t len, uint16_t *code)
{
  if (buf == NULL || code == NULL || len < 4)
    {
      return -EINVAL;
    }

  if ((buf[0] & 0x1f) != PW_BNEP_CONTROL || buf[1] != PW_BNEP_CTL_FILTER_RSP)
    {
      return -EINVAL;
    }

  *code = bnep_be16(&buf[2]);
  return 0;
}

int32_t pw_bnep_build_data(uint8_t *buf, uint32_t cap, const uint8_t *eth,
                           uint32_t ethlen)
{
  if (buf == NULL || eth == NULL || ethlen < PW_BNEP_HDR_LEN - 1 ||
      cap < ethlen + 1u)
    {
      return -EINVAL;
    }

  buf[0] = PW_BNEP_GENERAL_ETHERNET;
  memcpy(&buf[1], eth, ethlen);
  return (int32_t)(ethlen + 1u);
}
