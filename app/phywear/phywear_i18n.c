/****************************************************************************
 * apps/examples/phywear/phywear_i18n.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear i18n 表实现：EN/ZH 同时编译，运行时按 pw_i18n_set_zh() 切换。
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>

#include "phywear_i18n.h"

/* 自动生成的双语言表（按枚举名做指定初始化，顺序安全） */
#include "phywear_i18n_tables.inc"

static bool g_zh_lang;

void pw_i18n_set_zh(bool zh)
{
  g_zh_lang = zh;
}

bool pw_i18n_is_zh(void)
{
  return g_zh_lang;
}

const char *pw_str(int id)
{
  if (id < 0 || id >= PW_STR_COUNT)
    {
      return NULL;
    }

  return g_zh_lang ? g_pw_str_zh[id] : g_pw_str_en[id];
}
