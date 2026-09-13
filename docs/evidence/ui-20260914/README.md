# UI 提升第一批 + 两个隐患修复 —— 2026-09-14

| 文件 | 内容 |
|---|---|
| `main-menu-icons-gradient.png` | 主菜单：8 个板块加图标（LV_SYMBOL 字形 + 板块色淡底圆）、卡片加纵向渐变、AI 教练由灰字占位变可进入 |

本批改动（全部有真机固件复验）：

1. **主菜单图标**：`phywear_ui.c` 的 `pw_board_s` 增加 `icon` 字段，用内置 Montserrat 的
   `LV_SYMBOL_*` 字形（**不重生成中文字体子集**，零流程风险）：原始传感器=GPS、力学=REFRESH、
   声学=AUDIO、工具=EDIT、计时器=BELL、生活=HOME、自定义=PLUS、AI 教练=ENVELOPE。
2. **卡片纵向渐变**：`pw_card_new()` 加 2-stop 垂直渐变（`lv_color_lighten(bg,14)`）。
   EPIC 只对 2-stop H/V 渐变硬件加速，而 `radius≠0`/阴影会掉回软件光栅 —— 所以"渐变"
   是本板性价比最高、零帧率风险的美化手段。
3. **配色合规**：本作品 6 个传感器曲线色曾与 phyphox 色板**逐位相同**（含其品牌主色 `#ff7e22`），
   已整体平移为自选配色；并补 `app/phywear/NOTICE.md`。
4. **`pw_scope` 槽位泄漏修复**：曲线页面反复进出 6 次后曲线会静默消失（6 槽无释放路径）；
   现绑定 LVGL `LV_EVENT_DELETE` 自动释放并复用槽位（详见 `docs/03 §6.10`）。

> 截图为**模拟器**（390×450，与真机同分辨率同布局）；真机固件已烧录并复验（分块读回逐字节一致、启动到 NSH、`phywear micread` 正常）。

## 第二批（按用户反馈，09-14 02:xx）

| 反馈 | 处理 |
|---|---|
| 上下渐变"显得很奇怪" | **已移除**卡片纵向渐变，回到纯色卡片 |
| 「力学」的旋转箭头不符 | 改为**自绘单摆**（摆线 + 摆球，LVGL 图元，不依赖字体/图标库） |
| 「AI 教练」的信封不贴切 | 改为**自绘对话气泡 + 三个点** |
| 真机点「读加速度」回 `error=accelerometer not available` | **真 bug，已修**：NuttX 的文件描述符属于任务组，AI Agent 是独立任务 → GUI 打开的 `g_imu_fd` 在 Agent 任务里无效。新增 `pw_sensors_read_imu_oneshot()`（在调用者任务里 open/read/close），工具两处调用改用它 |

见 `main-menu-icons-v2.png`。
