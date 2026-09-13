# A2 证据：PhyWear 主动巡检（设备自触发 → Agent 执行）

| 步骤 | 实测 |
|---|---|
| 1 设备自己发现摆动 | PhyWear 巡检：检测到持续摆动（窗口振幅 0.54 g，持续 2.5 s）。请跑单摆实验测重力加速度 g 并解释结果。 |
| 2 Agent 转成工具调用（离线意图，无需 LLM） | phywear_run_experiment |
| 3 实验结果回传给 PhyWear（status=0） | `{"accepted":true,"screen":"pendulum","seconds":10,"samples":500,"unit":"mg","ax_mean":0.548,"ay_mean":0.134,"az_mean":99` |

完整串口日志：`console.log`
