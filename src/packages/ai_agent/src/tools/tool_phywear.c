/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* PhyWear tools: drive the wrist physics workshop from natural language.
 *
 * Design notes
 * ------------
 *  - LVGL is not thread safe and the PhyWear GUI owns the console, so screen
 *    switching is NOT done here.  We post a request into the PhyWear mailbox
 *    (pw_ai_request_open) and the GUI loop performs it on its next tick.
 *  - Sensor reads are plain character-device reads, so they may be issued
 *    from the agent thread directly.
 *  - Everything degrades gracefully: if the PhyWear app was not built or its
 *    UI is not running, each tool answers with a readable JSON error instead
 *    of failing the agent turn.
 */

#include "tools/tool_phywear.h"
#include "agent_compat.h"

#include <string.h>

#include "cJSON.h"

#ifdef CONFIG_EXAMPLES_PHYWEAR

/* Provided by apps/examples/phywear (declared here to avoid a hard build
 * dependency on the app's private headers). */

extern int         pw_ai_screen_count(void);
extern const char *pw_ai_screen_name(int idx);
extern const char *pw_ai_screen_desc(int idx);
extern int         pw_ai_request_open(const char *screen);
extern bool        pw_ai_gui_running(void);
extern const char *pw_ai_current_screen(void);

struct pw_ai_imu_s
{
  int ax;
  int ay;
  int az;
  int gx;
  int gy;
  int gz;
};

struct pw_ai_mag_s
{
  int x;
  int y;
  int z;
};

struct pw_ai_light_s
{
  unsigned int lux;
  int ch0;
  int ch1;
};

extern int pw_sensors_read_imu(struct pw_ai_imu_s *out);
extern int pw_sensors_read_mag(struct pw_ai_mag_s *out);
extern int pw_sensors_read_light(struct pw_ai_light_s *out);

#endif /* CONFIG_EXAMPLES_PHYWEAR */

/****************************************************************************
 * Helpers
 ****************************************************************************/

static void pw_tool_emit(cJSON *root, char *output, size_t output_size,
                         int status)
{
  char *s;

  (void)status;

  if (root == NULL)
    {
      snprintf(output, output_size, "{\"error\":\"out of memory\"}");
      return;
    }

  s = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (s != NULL)
    {
      strncpy(output, s, output_size - 1);
      output[output_size - 1] = '\0';
      free(s);
    }
  else
    {
      snprintf(output, output_size, "{\"error\":\"serialization failed\"}");
    }
}

static void pw_tool_emit_str(const char *key, const char *val, char *output,
                             size_t output_size)
{
  cJSON *r = cJSON_CreateObject();

  if (r != NULL)
    {
      cJSON_AddStringToObject(r, key, val);
    }

  pw_tool_emit(r, output, output_size, OK);
}

/****************************************************************************
 * phywear_list_experiments
 ****************************************************************************/

int tool_phywear_list_execute(const char *input_json, char *output,
                              size_t output_size)
{
  (void)input_json;

#ifdef CONFIG_EXAMPLES_PHYWEAR
  cJSON *root = cJSON_CreateObject();
  cJSON *arr;
  int i;

  if (root == NULL)
    {
      return ERROR;
    }

  arr = cJSON_AddArrayToObject(root, "screens");

  for (i = 0; i < pw_ai_screen_count(); i++)
    {
      cJSON *item = cJSON_CreateObject();

      cJSON_AddStringToObject(item, "name", pw_ai_screen_name(i));
      cJSON_AddStringToObject(item, "description", pw_ai_screen_desc(i));
      cJSON_AddItemToArray(arr, item);
    }

  cJSON_AddBoolToObject(root, "ui_running", pw_ai_gui_running());
  cJSON_AddStringToObject(root, "current_screen", pw_ai_current_screen());
  cJSON_AddStringToObject(root, "hint",
                          "Use phywear_open_screen with one of these names "
                          "to bring that page up on the watch display.");

  pw_tool_emit(root, output, output_size, OK);
  return OK;
#else
  pw_tool_emit_str("error", "PhyWear is not built into this image", output,
                   output_size);
  return ERROR;
#endif
}

/****************************************************************************
 * phywear_open_screen
 ****************************************************************************/

int tool_phywear_open_execute(const char *input_json, char *output,
                              size_t output_size)
{
#ifdef CONFIG_EXAMPLES_PHYWEAR
  /* Copy the parsed string out of the cJSON tree immediately: the tree is
   * released before the value is reported back, and holding a pointer into it
   * would be a use-after-free (KASAN catches it on the emulator). */

  char screen[32];
  bool have_screen = false;
  cJSON *root;
  cJSON *item;
  int ret;

  screen[0] = '\0';

  root = cJSON_Parse(input_json);
  if (root != NULL)
    {
      item = cJSON_GetObjectItem(root, "screen");
      if (item != NULL && cJSON_IsString(item))
        {
          const char *v = cJSON_GetStringValue(item);

          if (v != NULL)
            {
              strncpy(screen, v, sizeof(screen) - 1);
              screen[sizeof(screen) - 1] = '\0';
              have_screen = true;
            }
        }

      cJSON_Delete(root);
    }

  if (!have_screen)
    {
      pw_tool_emit_str("error", "missing required parameter: screen", output,
                       output_size);
      return ERROR;
    }

  ret = pw_ai_request_open(screen);

  if (ret < 0)
    {
      cJSON *r = cJSON_CreateObject();

      if (r != NULL)
        {
          cJSON_AddStringToObject(r, "error",
                                  ret == -ENODEV
                                      ? "PhyWear UI is not running; start the "
                                        "phywear app first"
                                      : "unknown screen name");
          cJSON_AddStringToObject(r, "requested", screen);
          cJSON_AddBoolToObject(r, "ui_running", pw_ai_gui_running());
        }

      pw_tool_emit(r, output, output_size, ERROR);
      return ERROR;
    }

  {
    cJSON *r = cJSON_CreateObject();

    if (r != NULL)
      {
        cJSON_AddBoolToObject(r, "accepted", true);
        cJSON_AddStringToObject(r, "screen", screen);
        cJSON_AddStringToObject(r, "previous_screen", pw_ai_current_screen());
        cJSON_AddStringToObject(r, "note",
                                "The page is switched by the PhyWear UI loop "
                                "within about 100 ms.");
      }

    pw_tool_emit(r, output, output_size, OK);
  }

  return OK;
#else
  pw_tool_emit_str("error", "PhyWear is not built into this image", output,
                   output_size);
  return ERROR;
#endif
}

/****************************************************************************
 * phywear_read_sensor
 ****************************************************************************/

int tool_phywear_sensor_execute(const char *input_json, char *output,
                                size_t output_size)
{
#ifdef CONFIG_EXAMPLES_PHYWEAR
  const char *sensor = NULL;
  cJSON *root;
  cJSON *item;
  cJSON *r;

  root = cJSON_Parse(input_json);
  if (root != NULL)
    {
      item = cJSON_GetObjectItem(root, "sensor");
      if (item != NULL && cJSON_IsString(item))
        {
          sensor = cJSON_GetStringValue(item);
        }
    }

  if (sensor == NULL)
    {
      cJSON_Delete(root);
      pw_tool_emit_str("error", "missing required parameter: sensor", output,
                       output_size);
      return ERROR;
    }

  r = cJSON_CreateObject();
  if (r == NULL)
    {
      cJSON_Delete(root);
      return ERROR;
    }

  if (strcmp(sensor, "accel") == 0 || strcmp(sensor, "acceleration") == 0)
    {
      struct pw_ai_imu_s imu;

      memset(&imu, 0, sizeof(imu));
      if (pw_sensors_read_imu(&imu) < 0)
        {
          cJSON_AddStringToObject(r, "error", "accelerometer not available");
          pw_tool_emit(r, output, output_size, ERROR);
          cJSON_Delete(root);
          return ERROR;
        }

      cJSON_AddStringToObject(r, "sensor", "accel");
      cJSON_AddStringToObject(r, "unit", "mg");
      cJSON_AddNumberToObject(r, "ax", (double)imu.ax);
      cJSON_AddNumberToObject(r, "ay", (double)imu.ay);
      cJSON_AddNumberToObject(r, "az", (double)imu.az);
      cJSON_AddNumberToObject(r, "gx", (double)imu.gx);
      cJSON_AddNumberToObject(r, "gy", (double)imu.gy);
      cJSON_AddNumberToObject(r, "gz", (double)imu.gz);
      cJSON_AddStringToObject(r, "gyro_unit", "mdps");
    }
  else if (strcmp(sensor, "mag") == 0 ||
           strcmp(sensor, "magnetometer") == 0)
    {
      struct pw_ai_mag_s mag;

      memset(&mag, 0, sizeof(mag));
      if (pw_sensors_read_mag(&mag) < 0)
        {
          cJSON_AddStringToObject(r, "error", "magnetometer not available");
          pw_tool_emit(r, output, output_size, ERROR);
          cJSON_Delete(root);
          return ERROR;
        }

      cJSON_AddStringToObject(r, "sensor", "mag");
      cJSON_AddStringToObject(r, "unit", "mG");
      cJSON_AddNumberToObject(r, "x", (double)mag.x);
      cJSON_AddNumberToObject(r, "y", (double)mag.y);
      cJSON_AddNumberToObject(r, "z", (double)mag.z);
    }
  else if (strcmp(sensor, "light") == 0)
    {
      struct pw_ai_light_s light;

      memset(&light, 0, sizeof(light));
      if (pw_sensors_read_light(&light) < 0)
        {
          cJSON_AddStringToObject(r, "error", "light sensor not available");
          pw_tool_emit(r, output, output_size, ERROR);
          cJSON_Delete(root);
          return ERROR;
        }

      cJSON_AddStringToObject(r, "sensor", "light");
      cJSON_AddStringToObject(r, "unit", "lux");
      cJSON_AddNumberToObject(r, "lux", (double)light.lux);
    }
  else
    {
      cJSON_AddStringToObject(r, "error", "unknown sensor");
      cJSON_AddStringToObject(r, "requested", sensor);
      cJSON_AddStringToObject(r, "supported", "accel | mag | light");
      pw_tool_emit(r, output, output_size, ERROR);
      cJSON_Delete(root);
      return ERROR;
    }

  pw_tool_emit(r, output, output_size, OK);
  cJSON_Delete(root);
  return OK;
#else
  pw_tool_emit_str("error", "PhyWear is not built into this image", output,
                   output_size);
  return ERROR;
#endif
}

/****************************************************************************
 * phywear_run_experiment
 *
 * Opens the requested experiment page and samples the accelerometer for the
 * given number of seconds, so the agent gets real numbers to reason about.
 * The experiment's own on-screen result (period, g, radius ...) is produced
 * by the PhyWear UI itself and is not exposed through this tool yet.
 ****************************************************************************/

int tool_phywear_run_execute(const char *input_json, char *output,
                             size_t output_size)
{
#ifdef CONFIG_EXAMPLES_PHYWEAR
  const char *screen = NULL;
  int seconds = 5;
  int samples = 0;
  int i;
  int n;
  int ret;
  int axmin = 0, axmax = 0, aymin = 0, aymax = 0, azmin = 0, azmax = 0;
  long axsum = 0, aysum = 0, azsum = 0;
  double axmean, aymean, azmean;
  cJSON *root;
  cJSON *item;
  cJSON *r;

  root = cJSON_Parse(input_json);
  if (root != NULL)
    {
      item = cJSON_GetObjectItem(root, "screen");
      if (item != NULL && cJSON_IsString(item))
        {
          screen = cJSON_GetStringValue(item);
        }

      item = cJSON_GetObjectItem(root, "seconds");
      if (item != NULL && cJSON_IsNumber(item))
        {
          seconds = (int)item->valuedouble;
        }
    }

  if (screen == NULL)
    {
      cJSON_Delete(root);
      pw_tool_emit_str("error", "missing required parameter: screen", output,
                       output_size);
      return ERROR;
    }

  if (seconds < 1)
    {
      seconds = 1;
    }
  else if (seconds > 30)
    {
      seconds = 30;
    }

  ret = pw_ai_request_open(screen);
  if (ret < 0)
    {
      cJSON *e = cJSON_CreateObject();

      if (e != NULL)
        {
          cJSON_AddStringToObject(e, "error",
                                  ret == -ENODEV
                                      ? "PhyWear UI is not running; start the "
                                        "phywear app first"
                                      : "unknown screen name");
          cJSON_AddStringToObject(e, "requested", screen);
          cJSON_AddBoolToObject(e, "ui_running", pw_ai_gui_running());
        }

      pw_tool_emit(e, output, output_size, ERROR);
      cJSON_Delete(root);
      return ERROR;
    }

  /* Sample the IMU at about 50 Hz while the experiment page is on screen. */

  n = seconds * 50;
  for (i = 0; i < n; i++)
    {
      struct pw_ai_imu_s imu;

      memset(&imu, 0, sizeof(imu));
      if (pw_sensors_read_imu(&imu) == 0)
        {
          if (samples == 0)
            {
              axmin = axmax = imu.ax;
              aymin = aymax = imu.ay;
              azmin = azmax = imu.az;
            }
          else
            {
              if (imu.ax < axmin) axmin = imu.ax;
              if (imu.ax > axmax) axmax = imu.ax;
              if (imu.ay < aymin) aymin = imu.ay;
              if (imu.ay > aymax) aymax = imu.ay;
              if (imu.az < azmin) azmin = imu.az;
              if (imu.az > azmax) azmax = imu.az;
            }

          axsum += imu.ax;
          aysum += imu.ay;
          azsum += imu.az;
          samples++;
        }

      usleep(20 * 1000);
    }

  r = cJSON_CreateObject();
  if (r == NULL)
    {
      cJSON_Delete(root);
      return ERROR;
    }

  cJSON_AddBoolToObject(r, "accepted", true);
  cJSON_AddStringToObject(r, "screen", screen);
  cJSON_AddNumberToObject(r, "seconds", (double)seconds);
  cJSON_AddNumberToObject(r, "samples", (double)samples);

  if (samples > 0)
    {
      axmean = (double)axsum / (double)samples;
      aymean = (double)aysum / (double)samples;
      azmean = (double)azsum / (double)samples;

      cJSON_AddStringToObject(r, "unit", "mg");
      cJSON_AddNumberToObject(r, "ax_mean", axmean);
      cJSON_AddNumberToObject(r, "ay_mean", aymean);
      cJSON_AddNumberToObject(r, "az_mean", azmean);
      cJSON_AddNumberToObject(r, "ax_min", (double)axmin);
      cJSON_AddNumberToObject(r, "ax_max", (double)axmax);
      cJSON_AddNumberToObject(r, "ay_min", (double)aymin);
      cJSON_AddNumberToObject(r, "ay_max", (double)aymax);
      cJSON_AddNumberToObject(r, "az_min", (double)azmin);
      cJSON_AddNumberToObject(r, "az_max", (double)azmax);
      cJSON_AddStringToObject(r, "note",
                              "This is a raw accelerometer summary. The "
                              "experiment result (period, g, radius ...) is "
                              "computed and shown on the watch display.");
    }
  else
    {
      cJSON_AddStringToObject(r, "error",
                              "no accelerometer samples available");
    }

  pw_tool_emit(r, output, output_size, samples > 0 ? OK : ERROR);
  cJSON_Delete(root);
  return samples > 0 ? OK : ERROR;
#else
  pw_tool_emit_str("error", "PhyWear is not built into this image", output,
                   output_size);
  return ERROR;
#endif
}
