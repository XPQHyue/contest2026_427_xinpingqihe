/****************************************************************************
 * apps/examples/phywear/pw_analysis.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear 通用分析算法库（纯 C，无 NuttX/平台依赖，可主机单测）。
 * 算法移植自 phyphox（GNU GPL，RWTH Aachen），参考 phywear-docs/phyphox_reference.md。
 *
 * 覆盖：radix-2 复 FFT、FFT 主频检测、自相关、自相关周期检测。
 * 全部用 float（SF32LB52 单精度 FPU，精度足够；double 会退化到软浮点）。
 ****************************************************************************/

#ifndef __PHYWEAR_PW_ANALYSIS_H
#define __PHYWEAR_PW_ANALYSIS_H

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * FFT
 ****************************************************************************/

/* 就地计算 n 点复 FFT（radix-2，n 必须是 2 的幂）。
 * re[]/im[] 输入为实部/虚部数组，输出为频谱（未归一化）。
 * invert=0 正向变换；invert!=0 逆向变换。
 */
void pw_fft(float *re, float *im, unsigned n, int invert);

/* 对实信号 x[0..n-1]（n 任意），零填充到下一 2 幂后做 FFT，
 * 输出前 n/2 个频谱幅度（直流在第 0 点），返回值 = 幅度点数（n/2）。
 * 调用方保证 mag 至少有 n/2+1 个 float。
 */
unsigned pw_fft_magnitude(const float *x, unsigned n, float *mag);

/* 由频谱幅度数组找主频：返回峰值对应的连续频率（用抛物线插值细化），
 * bin 间隔 = samplerate/n。找不到（全 0）返回 0。 */
float pw_fft_dominant_freq(const float *mag, unsigned nbin,
                           float samplerate, unsigned n);

/****************************************************************************
 * 自相关与周期检测
 ****************************************************************************/

/* 时域自相关：对 y[0..n-1]，计算位移 lag=0..maxlag 的自相关
 * ac[lag] = Σ y[j]*y[j+lag] / (n-lag)  （归一化到重叠样本数）。
 * 调用方保证 ac 有 maxlag+1 个 float。 */
void pw_autocorr(const float *y, unsigned n, float *ac, unsigned maxlag);

/* 由自相关求主周期（摆/弹簧振荡共用）：
 *   min_lag：允许的最小周期（采样点），用于跳过零 lag 大峰附近的盲区
 *   max_lag：允许的最大周期（采样点）
 * 策略：找 [min_lag,max_lag] 内所有局部最大峰，取第一个显著峰作为粗估，
 *       再用后续第 k 个峰（k*T 处）÷k 精调，返回以采样点为单位的周期。
 * 找不到返回 0。 */
float pw_period_from_autocorr(const float *ac, unsigned maxlag,
                              unsigned min_lag, unsigned max_period);

/* 便捷：给定等间隔采样信号 y（共 n 点，采样间隔 dt 秒），直接返回主周期（秒）。
 * 内部自相关 + pw_period_from_autocorr。 */
float pw_signal_period(const float *y, unsigned n, float dt);

/****************************************************************************
 * 阈值触发与峰检测（秒表/磁性标尺地基，phyphox threshold 模块思路）
 ****************************************************************************/

/* 阈值触发状态机（带滞回 + 自动 re-arm）：
 *   rise=1（上升沿，运动/声学秒表）：v 必须先降到 off 以下（de-arm），
 *          随后升穿 on → 触发一次；触发后需再降回 off 以下才可再次触发。
 *   rise=0（下降沿，光学秒表光闸）：v 必须先升到 off 以上（de-arm），
 *          随后跌穿 on → 触发一次。
 * update 返回 1 = 本次产生触发沿；0 = 无。 */
struct pw_thresh_s
{
  float on;      /* 触发阈值 */
  float off;     /* re-arm 阈值（滞回） */
  int   rise;    /* 1=上升沿触发；0=下降沿触发 */
  int   armed;
};

void pw_thresh_init(struct pw_thresh_s *t, float on, float off, int rise);
int  pw_thresh_update(struct pw_thresh_s *t, float v);

/* 峰检测计数（磁性标尺：磁铁阵列扫过计数）：
 *   信号 x[0..n-1] 中，幅值高于 th_on 且回落到 th_off 以下才算完成一个峰；
 *   相邻峰完成位置至少间隔 min_gap 点（防毛刺/回声重复计数）。
 *   未回落的尾部峰不计（流式下一窗会再见到它）。返回峰个数。 */
unsigned pw_count_peaks(const float *x, unsigned n, float th_on,
                        float th_off, unsigned min_gap);

/* 滑动平均：y[i] = mean(x[max(0,i-win) .. min(n-1,i+win)])，端点截断。 */
void pw_moving_average(const float *x, unsigned n, float *y, unsigned win);

#ifdef __cplusplus
}
#endif

#endif /* __PHYWEAR_PW_ANALYSIS_H */
