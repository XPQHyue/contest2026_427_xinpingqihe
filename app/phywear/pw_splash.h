/****************************************************************************
 * apps/examples/phywear/pw_splash.h
 *
 * [AGENT-WIN11] 开机动画（splash）接口。上电自启后、主界面之前播放一段
 * logo + 进度条动画，用来覆盖 LCD 点亮到首帧之间那段黑屏。
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_SPLASH_H
#define __APPS_EXAMPLES_PHYWEAR_PW_SPLASH_H

#include <nuttx/config.h>

#ifdef CONFIG_EXAMPLES_PHYWEAR_SPLASH

/* 播放开机动画并阻塞到动画结束（内部驱动 lv_timer_handler） */

void pw_splash_show(int ms);

/* 主界面接管显示后调用，释放 splash 屏 */

void pw_splash_release(void);

#else

#  define pw_splash_show(ms)  do { } while (0)
#  define pw_splash_release() do { } while (0)

#endif

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_SPLASH_H */
