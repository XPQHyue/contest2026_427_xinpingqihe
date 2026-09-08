/****************************************************************************
 * apps/examples/phywear/phywear_i18n.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear i18n（接管重做，2026-09-04，维护者版）。
 *
 * 设计：
 *   - 单份 ID 枚举（仅包含代码实际引用且双语齐全的字符串）。
 *   - EN/ZH 两张 const 表同时编译进固件，运行时可切换，默认英文。
 *   - 调用点统一写 PW_STR(NAME)（宏展开为 pw_str(PW_STR_NAME)），
 *     新字符串只需加入生成表并同步更新生成脚本输入。
 *   - ID 清单/表数据为自动生成物（phywear_i18n_ids.inc、
 *     phywear_i18n_tables.inc），勿手改；来源见 i18n 表生成记录。
 *   - CJK 字体（中文 glyph）为阶段2 内容：未接入前中文在 Montserrat
 *     下显示为缺字方框，属已知限制（英文不受影响）。
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_I18N_H
#define __APPS_EXAMPLES_PHYWEAR_I18N_H

#include <stdbool.h>

/****************************************************************************
 * 字符串 ID 枚举（自动生成顺序）
 ****************************************************************************/

#define PW_I18N_ID(id) PW_STR_##id,
enum
{
#include "phywear_i18n_ids.inc"
  PW_STR_COUNT
};
#undef PW_I18N_ID

/****************************************************************************
 * 运行时语言控制
 ****************************************************************************/

/* 切换中文（true）/英文（false）。默认英文。 */
void pw_i18n_set_zh(bool zh);

/* 当前是否中文模式 */
bool pw_i18n_is_zh(void);

/* 取当前语言字符串；id 非法返回 NULL（调用方勿传非法 id） */
const char *pw_str(int id);

/* 调用点简写：PW_STR(UI_PHYWEAR) -> pw_str(PW_STR_UI_PHYWEAR) */
#define PW_STR(id) pw_str(PW_STR_##id)

#endif /* __APPS_EXAMPLES_PHYWEAR_I18N_H */
