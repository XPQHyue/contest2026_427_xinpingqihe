#!/usr/bin/env python3
"""三轴页曲线幅度的像素级校验（模拟器截图，可复现）。

做法：从截图里按系列色找出曲线像素，把像素行号按泳道几何反算成物理量，
再与同一帧上显示的数值 / 模拟器合成波形对照。

泳道几何（与 phywear_raw.c 的常量一致）：
  曲线区   screen x 16..373, y 166..352   (358×186)
  泳道 X   y 166..228   泳道 Y   y 228..290   泳道 Z   y 290..352
  泳道内映射  v_norm ∈[-1,1] →  y = top + (0.5 - v_norm*0.45) * (62-1)
  左侧/底部的 "+/-FS 单位" 标注给出满量程 FS。

用法： python3 verify_curve_scale.py <screenshot.png> <accel|gyro>
"""
import sys
from PIL import Image

SER = {'X': (255, 93, 123), 'Y': (90, 227, 140), 'Z': (90, 170, 255)}
LANES = {'X': (166, 228), 'Y': (228, 290), 'Z': (290, 352)}

# 模拟器合成波形（app/phywear/phywear_sensors.c）：
#   ax=600·sin  ay=120·sin  az=1000+300·sin  [mg]   gx=20000·sin [mdps]  gy=gz=0
EXPECT = {
    'accel': {'ax': 0.6, 'ay': 0.12, 'az': (0.7, 1.3), 'unit': 'g', 'fs': 2.0},
    'gyro':  {'ax': 20.0, 'ay': 0.0, 'az': 0.0, 'unit': 'dps', 'fs': 50.0},
}


def near(a, b, tol=30):
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


def main():
    path, kind = sys.argv[1], sys.argv[2]
    exp = EXPECT[kind]
    fs = exp['fs']
    px = Image.open(path).convert('RGB').load()
    print(f'{path}  ({kind}, 满量程 ±{fs} {exp["unit"]})')
    for ax, (t, b) in LANES.items():
        ys = [y for x in range(17, 373) for y in range(t + 1, b)
              if near(px[x, y], SER[ax])]
        if not ys:
            print(f'  {ax}: 无曲线像素')
            continue
        # 用出现次数最多的行作为"停留时间最长"的电平，极值行给出幅度
        from collections import Counter
        cnt = Counter(ys)
        lo, hi = min(ys), max(ys)

        def val(y):
            return ((0.5 - (y - t) / (b - t - 1)) / 0.45) * fs

        print(f'  {ax}: 曲线行 {lo}..{hi}  →  幅度 {val(hi):+.2f} .. {val(lo):+.2f}'
              f'  {exp["unit"]}   (最常见电平 {cnt.most_common(1)[0][0]}'
              f' → {val(cnt.most_common(1)[0][0]):+.2f})')
    print('  合成波形期望：' + str(EXPECT[kind]))


if __name__ == '__main__':
    main()
