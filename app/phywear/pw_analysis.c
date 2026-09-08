/****************************************************************************
 * apps/examples/phywear/pw_analysis.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear 通用分析算法实现。算法移植自 phyphox（GNU GPL，RWTH Aachen）。
 * 纯 C，float 运算，适配 Cortex-M33 单精度 FPU。
 ****************************************************************************/

#include "pw_analysis.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846f
#endif

/****************************************************************************
 * FFT（radix-2 迭代：位反转 + 蝶形）
 ****************************************************************************/

void pw_fft(float *re, float *im, unsigned n, int invert)
{
  unsigned i;
  unsigned j = 0;

  /* 位反转重排 */

  for (i = 0; i < n - 1; i++)
    {
      if (i < j)
        {
          float tr = re[i]; re[i] = re[j]; re[j] = tr;
          tr = im[i]; im[i] = im[j]; im[j] = tr;
        }

      unsigned m = n >> 1;
      while (m > 0 && (j & m))
        {
          j ^= m;
          m >>= 1;
        }

      j |= m;
    }

  /* 蝶形运算：len = 每次合并的区间长度的一半 */

  for (unsigned len = 1; len < n; len <<= 1)
    {
      /* 旋转因子（实时算 cos/sin，或用查表优化） */

      float ang = (invert ? 1.0f : -1.0f) * M_PI / (float)len;
      float w_re = cosf(ang);
      float w_im = sinf(ang);

      for (i = 0; i < n; i += 2 * len)
        {
          float cur_re = 1.0f;
          float cur_im = 0.0f;

          for (j = 0; j < len; j++)
            {
              unsigned even = i + j;
              unsigned odd  = i + j + len;

              /* odd * w */

              float tr = re[odd] * cur_re - im[odd] * cur_im;
              float ti = re[odd] * cur_im + im[odd] * cur_re;

              re[odd] = re[even] - tr;
              im[odd] = im[even] - ti;
              re[even] += tr;
              im[even] += ti;

              /* 更新旋转因子 */

              float nr = cur_re * w_re - cur_im * w_im;
              cur_im = cur_re * w_im + cur_im * w_re;
              cur_re = nr;
            }
        }
    }

  if (invert)
    {
      for (i = 0; i < n; i++)
        {
          re[i] /= (float)n;
          im[i] /= (float)n;
        }
    }
}

/****************************************************************************
 * 实信号 FFT 幅度谱（零填充到 2 的幂）
 ****************************************************************************/

unsigned pw_fft_magnitude(const float *x, unsigned n, float *mag)
{
  unsigned np2 = 1;
  float *re;
  float *im;
  unsigned i;
  unsigned half;

  /* 找下一 2 幂 */

  while (np2 < n)
    {
      np2 <<= 1;
    }

  re = (float *)malloc(2 * np2 * sizeof(float));
  if (re == NULL)
    {
      return 0;
    }

  im = re + np2;

  memset(re, 0, np2 * sizeof(float));
  memset(im, 0, np2 * sizeof(float));
  memcpy(re, x, n * sizeof(float));

  pw_fft(re, im, np2, 0);

  half = np2 / 2;
  for (i = 0; i <= half; i++)
    {
      mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]);
    }

  free(re);
  return half + 1;   /* 0..half 共 half+1 个点（含直流与奈奎斯特） */
}

/****************************************************************************
 * 频谱主频（抛物线插值细化）
 ****************************************************************************/

float pw_fft_dominant_freq(const float *mag, unsigned nbin,
                           float samplerate, unsigned n)
{
  unsigned i;
  unsigned peak = 0;
  float best = -1.0f;

  /* 跳过直流（bin 0），找最大幅度 bin */

  for (i = 1; i < nbin; i++)
    {
      if (mag[i] > best)
        {
          best = mag[i];
          peak = i;
        }
    }

  if (peak == 0 || peak >= nbin - 1 || best <= 0.0f)
    {
      return 0.0f;
    }

  /* 抛物线插值细化峰位：x0 = peak + 0.5*(m[peak-1]-m[peak+1])/
   *                                      (m[peak-1]-2*m[peak]+m[peak+1]) */

  float denom = mag[peak - 1] - 2.0f * mag[peak] + mag[peak + 1];
  float delta = 0.0f;

  if (fabsf(denom) > 1e-12f)
    {
      delta = 0.5f * (mag[peak - 1] - mag[peak + 1]) / denom;
    }

  float bin = (float)peak + delta;
  float df  = samplerate / (float)n;   /* 频域分辨率 */

  return bin * df;
}

/****************************************************************************
 * 时域自相关
 ****************************************************************************/

void pw_autocorr(const float *y, unsigned n, float *ac, unsigned maxlag)
{
  unsigned lag;
  unsigned j;

  if (maxlag > n - 1)
    {
      maxlag = n - 1;
    }

  for (lag = 0; lag <= maxlag; lag++)
    {
      float sum = 0.0f;
      unsigned cnt = n - lag;

      for (j = 0; j < cnt; j++)
        {
          sum += y[j] * y[j + lag];
        }

      ac[lag] = (cnt > 0) ? (sum / (float)cnt) : 0.0f;
    }
}

/****************************************************************************
 * 自相关 → 主周期（粗估 + 第 k 个峰精调）
 ****************************************************************************/

float pw_period_from_autocorr(const float *ac, unsigned maxlag,
                              unsigned min_lag, unsigned max_period)
{
  unsigned i;
  int rising = 0;
  float first_period = 0.0f;
  unsigned first_peak = 0;

  /* 显著性阈值：峰值自相关须超过 ac[0] 的一定比例才算有效周期峰
   * （噪声/静止时 ac[lag>0] 很小，可据此拒绝假周期）。
   * 0.25：白噪声 ac[lag>0]/ac[0] ≈ ±1/√n（n=512 → 0.044，峰值约
   * 5.7σ 外，误检概率≈0）；真实摆/弹簧自相关峰 0.6~1.0，远高于此。
   * 实测：0.12 时静止白噪有 ~2 个假峰（主机单测 FAIL），0.25 全部拒绝。
   * 弱信号由调用方 MOTION_STD 物理门槛兜底。 */

  float sig = ac[0] * 0.25f;
  if (sig <= 0.0f)
    {
      return 0.0f;
    }

  /* 1) 在 [min_lag, max_period] 内找第一个「显著」局部最大峰 */

  for (i = min_lag; i <= max_period; i++)
    {
      if (ac[i] > ac[i - 1] && !rising)
        {
          rising = 1;
        }

      if (rising && ac[i] < ac[i - 1])
        {
          /* 越过峰顶（ac[i-1] 是局部峰） */

          rising = 0;

          if (ac[i - 1] < sig)
            {
              continue;           /* 不显著，跳过 */
            }

          first_peak = i - 1;
          first_period = (float)first_peak;

          /* 抛物线插值细化峰位 */

          if (first_peak > 0 && first_peak < maxlag)
            {
              float denom = ac[first_peak - 1] - 2.0f * ac[first_peak] +
                            ac[first_peak + 1];
              if (fabsf(denom) > 1e-12f)
                {
                  first_period = (float)first_peak +
                    0.5f * (ac[first_peak - 1] - ac[first_peak + 1]) / denom;
                }
            }

          break;
        }
    }

  if (first_period <= 0.0f)
    {
      return 0.0f;
    }

  /* 2) 精调：后续自相关峰位于整数倍 k*T。找 max_period 内最后一个
   *    显著且接近整数倍的峰 peak_k，取 k = round(peak_k / first_period)，
   *    返回 peak_k / k —— 分辨率提升约 k 倍（phyphox pendulum 同思路）。 */

  float refined = first_period;
  unsigned best_peak = first_peak;
  float best_k = 1.0f;

  /* 扫描后续峰，记录显著且接近整数倍的峰 */

  rising = 0;
  for (i = first_peak + 1; i < max_period; i++)
    {
      if (ac[i] > ac[i - 1] && !rising)
        {
          rising = 1;
        }

      if (rising && ac[i] < ac[i - 1])
        {
          unsigned pk = i - 1;
          float k = (float)pk / first_period;
          float kint = roundf(k);

          rising = 0;

          if (ac[pk] > sig && kint >= 2.0f && fabsf(k - kint) < 0.15f)
            {
              /* 该峰显著且接近整数倍 → 用它精调（取最后一个有效者） */

              best_peak = pk;
              best_k = kint;
            }
        }
    }

  if (best_peak != first_peak && best_k >= 2.0f)
    {
      /* 用第 k 个峰的峰位（已抛物线插值）÷k */

      float kden = 0.0f;

      if (best_peak > 0 && best_peak < maxlag)
        {
          kden = ac[best_peak - 1] - 2.0f * ac[best_peak] +
                 ac[best_peak + 1];
        }

      if (fabsf(kden) > 1e-12f)
        {
          float pk_fine = (float)best_peak +
            0.5f * (ac[best_peak - 1] - ac[best_peak + 1]) / kden;
          refined = pk_fine / best_k;
        }
      else
        {
          refined = (float)best_peak / best_k;
        }
    }

  return refined;
}

/****************************************************************************
 * 便捷：信号主周期
 ****************************************************************************/

float pw_signal_period(const float *y, unsigned n, float dt)
{
  unsigned maxlag = (n > 2) ? (n - 1) : 0;
  float *work;
  float *ac;
  float mean = 0.0f;
  float period;
  unsigned i;
  unsigned min_lag;
  unsigned max_period;

  if (n < 32)
    {
      return 0.0f;
    }

  work = (float *)malloc(n * sizeof(float));
  ac   = (float *)malloc((maxlag + 1) * sizeof(float));
  if (work == NULL || ac == NULL)
    {
      free(work);
      free(ac);
      return 0.0f;
    }

  /* 去均值（去掉陀螺零偏/重力直流），否则直流会污染自相关 */

  for (i = 0; i < n; i++)
    {
      mean += y[i];
    }

  mean /= (float)n;

  for (i = 0; i < n; i++)
    {
      work[i] = y[i] - mean;
    }

  pw_autocorr(work, n, ac, maxlag);

  /* min_lag：假设最短周期 0.2s（摆/弹簧典型 0.3s+，太短易误检噪声）；
   * max_period：最长 5s */

  min_lag    = (unsigned)(0.2f / dt);
  max_period = (unsigned)(5.0f / dt);
  if (max_period > maxlag)
    {
      max_period = maxlag;
    }

  period = pw_period_from_autocorr(ac, maxlag, min_lag, max_period);

  free(work);
  free(ac);
  return period * dt;
}

/****************************************************************************
 * 阈值触发与峰检测
 ****************************************************************************/

void pw_thresh_init(struct pw_thresh_s *t, float on, float off, int rise)
{
  if (t == NULL)
    {
      return;
    }

  t->on = on;
  t->off = off;
  t->rise = rise ? 1 : 0;
  t->armed = 0;
}

int pw_thresh_update(struct pw_thresh_s *t, float v)
{
  if (t == NULL)
    {
      return 0;
    }

  if (t->rise)
    {
      if (!t->armed)
        {
          /* de-arm：低于 off 后允许触发 */
          if (v < t->off)
            {
              t->armed = 1;
            }
        }
      else if (v > t->on)
        {
          t->armed = 0;   /* 触发后需重新 de-arm（防粘连重复触发） */
          return 1;
        }
    }
  else
    {
      if (!t->armed)
        {
          if (v > t->off)
            {
              t->armed = 1;
            }
        }
      else if (v < t->on)
        {
          t->armed = 0;
          return 1;
        }
    }

  return 0;
}

unsigned pw_count_peaks(const float *x, unsigned n, float th_on,
                        float th_off, unsigned min_gap)
{
  unsigned count = 0;
  unsigned last_end = 0;
  unsigned i;
  int in = 0;

  if (x == NULL)
    {
      return 0;
    }

  for (i = 0; i < n; i++)
    {
      if (!in)
        {
          if (x[i] > th_on)
            {
              in = 1;
            }
        }
      else if (x[i] < th_off)
        {
          /* 峰完成：间隔检查（count==0 为首峰，无间隔要求） */
          if (count == 0 || i - last_end >= min_gap)
            {
              count++;
              last_end = i;
            }

          in = 0;
        }
    }

  return count;
}

void pw_moving_average(const float *x, unsigned n, float *y, unsigned win)
{
  unsigned i;
  unsigned k;

  if (x == NULL || y == NULL || n == 0)
    {
      return;
    }

  for (i = 0; i < n; i++)
    {
      unsigned lo = (i > win) ? (i - win) : 0;
      unsigned hi = ((i + win) < n) ? (i + win) : (n - 1);
      float acc = 0.0f;

      for (k = lo; k <= hi; k++)
        {
          acc += x[k];
        }

      y[i] = acc / (float)(hi - lo + 1);
    }
}
