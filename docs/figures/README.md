# 图片素材（技术报告配图）

本目录存放技术报告使用的系统框图与数据流图（本队原创绘制）。

| 文件 | 用途 | 在报告中的位置 |
|---|---|---|
| `fig1_architecture.png` | **系统总体架构框图**（分层：应用层 / 图形栈 / 显示驱动 / 传感器驱动 / 系统硬件；绿=本队原创，橙=官方提供） | 3.2 系统方案设计（**图 1**） |
| `fig2_dataflow.png` | **数据流与关键流程**（传感器 → 设备节点 → pw_sensors → 页面/算法 → LVGL 控件 → EPIC → AMOLED + 5 步关键流程） | 3.4 系统实现（**图 2**） |

> 报告中另嵌入了 3 张实测证据图（图 3 全中文根菜单、图 4 原始传感器页、图 5 渲染帧率时序），
> 其源文件分别位于 `docs/evidence/` 与 `docs/project/fps_timeline.png`。

## 技术报告内嵌图片清单（图 1–图 5）

| 图号 | 内容 | 来源 |
|---|---|---|
| 图 1 | 系统总体架构 | 本目录 `fig1_architecture.png` |
| 图 2 | 数据流与关键流程 | 本目录 `fig2_dataflow.png` |
| 图 3 | 全中文根菜单（读 /dev/fb0） | `docs/evidence/phy_core_root_zh.png` |
| 图 4 | 原始传感器页（加速度计，全中文） | `docs/evidence/phy_core_raw_zh.png` |
| 图 5 | 渲染帧率时序（min 22 / target 43） | `docs/project/fps_timeline.png` |
