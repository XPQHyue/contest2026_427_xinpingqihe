/* pw_analysis 主机单测：合成信号验证 FFT 主频 + 自相关周期检测
 * + 阈值触发状态机 + 峰检测计数 + 滑动平均。
 * 编译：gcc -o pw_test pw_analysis_test_host.c pw_analysis.c -lm */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pw_analysis.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 生成含噪声/直流的正弦：f=频率, fs=采样率, n=点数, dc=直流, noise=噪声幅度 */
static void gen_sine(float *y, int n, float fs, float f, float dc, float noise)
{
  for (int i = 0; i < n; i++)
    {
      y[i] = dc + sinf(2.0f * (float)M_PI * f * i / fs)
             + noise * (((float)rand() / RAND_MAX) - 0.5f) * 2.0f;
    }
}

static int test_fft(const char *name, float f, float fs, int n, float noise)
{
  float *y = malloc(n * sizeof(float));
  float *mag = malloc((n / 2 + 2) * sizeof(float));
  float found;
  int pass;

  gen_sine(y, n, fs, f, 0.0f, noise);
  unsigned nb = pw_fft_magnitude(y, (unsigned)n, mag);
  found = pw_fft_dominant_freq(mag, nb, fs, (unsigned)n);
  pass = (nb > 0) && (fabsf(found - f) / f < 0.02f);   /* 2% 误差内 */
  printf("[%s] 真实频率=%.1fHz 检出=%.1fHz 点数=%u → %s\n",
         name, f, found, nb, pass ? "PASS" : "FAIL");
  free(y); free(mag);
  return pass;
}

static int test_period(const char *name, float f, float fs, int n, float dc)
{
  float *y = malloc(n * sizeof(float));
  float T;
  int pass;

  gen_sine(y, n, fs, f, dc, 0.02f);
  T = pw_signal_period(y, (unsigned)n, 1.0f / fs);
  pass = (T > 0) && (fabsf(T - 1.0f / f) / (1.0f / f) < 0.03f);
  printf("[%s] 真实T=%.4fs 检出T=%.4fs → %s\n",
         name, 1.0f / f, T, pass ? "PASS" : "FAIL");
  free(y);
  return pass;
}

static int test_noperiod(const char *name, float *y, int n, float dt)
{
  float T = pw_signal_period(y, n, dt);
  int pass = (T == 0.0f);
  printf("[%s] 检出T=%.4f → %s\n", name, T, pass ? "PASS(无周期)" : "FAIL(误检)");
  return pass;
}

/* 上升沿触发：低于 off 才 de-arm；穿 on 触发一次；未回落不重复触发 */
static int test_thresh_rise(void)
{
  struct pw_thresh_s t;
  int trig = 0;
  int pass;

  pw_thresh_init(&t, 0.5f, 0.3f, 1);
  trig += pw_thresh_update(&t, 0.1f);   /* de-arm */
  trig += pw_thresh_update(&t, 0.2f);   /* armed，未穿 */
  trig += pw_thresh_update(&t, 0.6f);   /* 触发 1 */
  trig += pw_thresh_update(&t, 0.8f);   /* 未 re-arm，不触发 */
  trig += pw_thresh_update(&t, 0.7f);   /* 不触发 */
  trig += pw_thresh_update(&t, 0.1f);   /* re-arm */
  trig += pw_thresh_update(&t, 0.4f);   /* armed，未穿 */
  trig += pw_thresh_update(&t, 0.55f);  /* 触发 2 */
  pass = (trig == 2);
  printf("[上升沿] 触发次数=%d (期望2) → %s\n", trig, pass ? "PASS" : "FAIL");
  return pass;
}

/* 下降沿触发（光学秒表光闸） */
static int test_thresh_fall(void)
{
  struct pw_thresh_s t;
  int trig = 0;
  int pass;

  pw_thresh_init(&t, 0.3f, 0.6f, 0);
  trig += pw_thresh_update(&t, 0.8f);   /* de-arm */
  trig += pw_thresh_update(&t, 0.7f);   /* armed，未穿 */
  trig += pw_thresh_update(&t, 0.2f);   /* 触发 1 */
  trig += pw_thresh_update(&t, 0.1f);   /* 未 re-arm */
  trig += pw_thresh_update(&t, 0.9f);   /* re-arm */
  trig += pw_thresh_update(&t, 0.4f);   /* armed，未穿 */
  trig += pw_thresh_update(&t, 0.25f);  /* 触发 2 */
  pass = (trig == 2);
  printf("[下降沿] 触发次数=%d (期望2) → %s\n", trig, pass ? "PASS" : "FAIL");
  return pass;
}

/* 在 x[lo..hi] 内生成三角峰，峰顶 mid、高 amp、底 baseline */
static void tri_peak(float *x, int lo, int mid, int hi, float amp)
{
  int i;

  for (i = lo; i <= hi; i++)
    {
      float f = (i <= mid) ? (float)(i - lo) / (float)(mid - lo)
                           : (float)(hi - i) / (float)(hi - mid);
      x[i] = amp * f;
    }
}

/* 峰计数：两个孤立峰 + 一个太近毛刺 + 一个未回落尾峰 */
static int test_peaks(void)
{
  float x[220];
  unsigned count;
  int i;
  int pass;

  for (i = 0; i < 220; i++)
    {
      x[i] = 0.0f;
    }

  tri_peak(x, 20, 30, 40, 1.0f);    /* 峰1：回落@~39 */
  tri_peak(x, 80, 90, 100, 0.9f);   /* 峰2：回落@~98，与峰1 差 ~59 > 25 */
  tri_peak(x, 118, 120, 122, 0.8f); /* 毛刺：回落@~122，与峰2 差 ~24 < 25 → 排除 */
  for (i = 160; i <= 219; i++)      /* 尾峰：平台 0.8 不回落 → 不计 */
    {
      x[i] = 0.8f;
    }

  count = pw_count_peaks(x, 220, 0.5f, 0.2f, 25);
  pass = (count == 2);
  printf("[峰计数] 检出=%u (期望2) → %s\n", count, pass ? "PASS" : "FAIL");
  return pass;
}

static int test_ma(void)
{
  float x[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float y[8];
  int pass;

  pw_moving_average(x, 8, y, 1);
  pass = (fabsf(y[0] - 1.5f) < 1e-4f) &&
         (fabsf(y[3] - 4.0f) < 1e-4f) &&
         (fabsf(y[7] - 7.5f) < 1e-4f);
  printf("[滑动平均] y[0]=%.1f y[3]=%.1f y[7]=%.1f → %s\n",
         y[0], y[3], y[7], pass ? "PASS" : "FAIL");
  return pass;
}

int main(void)
{
  int ok = 1;
  float fs = 50.0f;   /* 与摆测g一致 50Hz */

  srand(42);

  printf("=== FFT 主频测试（fs=50Hz）===\n");
  ok &= test_fft("FFT 1.0Hz 纯", 1.0f, fs, 512, 0.0f);
  ok &= test_fft("FFT 2.3Hz 纯", 2.3f, fs, 256, 0.0f);
  ok &= test_fft("FFT 1.0Hz+噪声", 1.0f, fs, 512, 0.3f);

  printf("\n=== 自相关周期测试（fs=50Hz，含直流偏置）===\n");
  ok &= test_period("周期 T=0.8s(1.25Hz) 直流0.5", 1.25f, fs, 512, 0.5f);
  ok &= test_period("周期 T=1.0s(1.0Hz) 直流1g", 1.0f, fs, 512, 1.0f);
  ok &= test_period("周期 T=0.5s(2.0Hz) 无直流", 2.0f, fs, 256, 0.0f);
  ok &= test_period("周期 T=1.6s(0.625Hz)", 0.625f, fs, 800, 0.2f);

  printf("\n=== 静止/纯噪声（应无周期）===\n");
  {
    float *noise = malloc(512 * sizeof(float));
    for (int i = 0; i < 512; i++)
      noise[i] = 0.8f + 0.05f * (((float)rand()/RAND_MAX)-0.5f);
    ok &= test_noperiod("静止(偏置0.8+微噪)", noise, 512, 0.02f);
    free(noise);
  }

  printf("\n=== 阈值触发状态机（秒表地基）===\n");
  ok &= test_thresh_rise();
  ok &= test_thresh_fall();

  printf("\n=== 峰检测计数（磁性标尺地基）===\n");
  ok &= test_peaks();

  printf("\n=== 滑动平均 ===\n");
  ok &= test_ma();

  printf("\n结果: %s\n", ok ? "全部 PASS ✅" : "有 FAIL ❌");
  return ok ? 0 : 1;
}
