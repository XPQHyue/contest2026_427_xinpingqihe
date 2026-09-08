# ##############################################################################
# apps/examples/phywear/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
# ##############################################################################

if(CONFIG_EXAMPLES_PHYWEAR)
  nuttx_add_application(
    NAME
    phywear
    PRIORITY
    ${CONFIG_EXAMPLES_PHYWEAR_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_PHYWEAR_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_PHYWEAR}
    DEPENDS
    lvgl
    SRCS
    phywear.c
    phywear_i18n.c
    phywear_sensors.c
    pw_font_14.c
    pw_font_16.c
    pw_font_20.c
    pw_font_24.c
    pw_font_28.c
    phywear_ui.c
    phywear_raw.c
    phywear_pend.c
    phywear_spec.c
    phywear_spring.c
    phywear_centri.c
    phywear_incline.c
    phywear_ruler.c
    phywear_time.c
    phywear_life.c
    pw_scope.c
    pw_graph.c
    pw_analysis.c)
endif()
