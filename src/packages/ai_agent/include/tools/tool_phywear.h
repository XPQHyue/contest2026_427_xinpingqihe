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

#ifndef __TOOLS_TOOL_PHYWEAR_H
#define __TOOLS_TOOL_PHYWEAR_H

#include <stddef.h>

/* PhyWear wrist physics workshop: let the agent drive the on-device
 * experiment UI and read the board sensors.
 *
 * All four tools return a JSON string in `output` and never touch LVGL from
 * the agent thread: screen switching is forwarded to the PhyWear GUI loop
 * through the pw_ai mailbox (see apps/examples/phywear/pw_ai.h).  When the
 * PhyWear UI is not running the tools report a clear error instead.
 */

int tool_phywear_list_execute(const char *input_json, char *output, size_t output_size);
int tool_phywear_open_execute(const char *input_json, char *output, size_t output_size);
int tool_phywear_sensor_execute(const char *input_json, char *output, size_t output_size);
int tool_phywear_run_execute(const char *input_json, char *output, size_t output_size);

#endif /* __TOOLS_TOOL_PHYWEAR_H */
