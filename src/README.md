# src/ — 全量源码快照

本目录集中存放**本队对 openvela 公共仓的技术改动**（新传感器驱动、黄山派 BSP、
SF32LB52 芯片层 EPIC 加速、LVGL EPIC 加速后端、goldfish-phywear 模拟器配置），
供评委直接审阅源码、核实工作量。

- **原创应用**（PhyWear LVGL，42 文件）在仓库根 `app/phywear/`，不在此目录。
- 每个文件的**来源路径 / 所属分支 / 关键 commit** 见 [`MANIFEST.md`](MANIFEST.md)。

## 布局
```
src/
├── nuttx/                  # 新传感器驱动（mmc5603 / ltr303 / lsm6dsl）+ 头文件
├── vendor/sifli/           # 黄山派 BSP（lckfb_huangshan_pi）+ SF32LB52 芯片层 EPIC
├── vendor/openvela/        # goldfish-phywear 模拟器板级配置
└── lvgl/                   # LVGL SiFli EPIC 硬件加速后端（draw/sifli）
```

> 合规：app/phywear 为本队原创；公共仓改动以源码快照纳入并在 MANIFEST 载明对应
> openvela 分支 / commit，便于按官方流程 review 合入上游。
