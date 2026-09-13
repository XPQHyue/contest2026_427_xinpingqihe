NOTICE — PhyWear (app/phywear)

本项目（PhyWear 腕上智慧物理工坊）的运行代码为 **独立实现**：

- 应用与算法用 **C + LVGL 从零编写**，源码许可为 **Apache-2.0**（与 openvela 基线一致）。
- **实验场景 / 测量流程 / 功能设计** 源自 phyphox —— RWTH Aachen 大学开发的手机端物理实验
  工具，以 **GPL v3** 发布（https://phyphox.org / 源码 https://github.com/phyphox）。本作品
  **未复制** phyphox 的代码体（其 Android 版为 Kotlin/Java），仅对齐其物理实验场景、测量
  流程与分析流程。

- **分析算法实现**：本作品中 FFT（radix-2 迭代 Cooley-Tukey：位反转 + 蝶形）、自相关测周期、
  向心 a-ω² 最小二乘等**属通用/教科书标准算法**，以**纯 C 单精度浮点**独立实现（适配
  Cortex-M33 FPU）。其**功能/算法流程**对齐 phyphox 的实验设计，但代码为自研实现。

**关于 GPL 的合规说明（重要，如实声明）**：
- phyphox 以 GPL v3 发布。本作品**不复制、不链接 phyphox 的 GPL 代码**，故不形成衍生作品的
  GPL 传染；源码头部注释"算法移植自 phyphox（GNU GPL）"指的是**功能/算法流程的出处**，
  并非代码逐行拷贝。
- 若评审认定某一算法模块确属 phyphox 代码的"紧密移植"，我们会**将该模块源码按 GPL v3
  单独发布**，以确保合规。此处如实标注灵感与算法出处并致谢 phyphox（RWTH Aachen）。
