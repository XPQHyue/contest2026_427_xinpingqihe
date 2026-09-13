/****************************************************************************
 * apps/examples/phywear/pw_skill.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Publish the PhyWear Markdown Skills to the AI Agent.
 *
 * The AI Agent (packages/ai_agent) discovers skills as Markdown files in
 * /data/agent/skills/ and describes them to the model in its system prompt.
 * The skill sources live in apps/examples/phywear/skills/*.md and are embedded
 * into the firmware as C strings by tools/phywear/gen_skill_blob.py.
 * pw_skill_install() writes them to the agent directory at startup, which
 * matters on real hardware because /data is a tmpfs there and would otherwise
 * be empty after every boot.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_SKILL_H
#define __APPS_EXAMPLES_PHYWEAR_PW_SKILL_H

/* Skill contents generated from apps/examples/phywear/skills/*.md. */

extern const char g_pw_skill_phywear_physics_coach[];

/****************************************************************************
 * Name: pw_skill_install
 *
 * Description:
 *   Create the AI Agent skill directory and install every embedded skill
 *   whose on-device copy is missing or different.  Existing files are left
 *   untouched (including their timestamps) when the contents already match,
 *   so the agent's skill hot-reload is not triggered on every boot.
 *
 * Returned Value:
 *   The number of skills written, or a negated errno value if the skill
 *   directory could not be created.
 *
 ****************************************************************************/

int pw_skill_install(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_SKILL_H */
