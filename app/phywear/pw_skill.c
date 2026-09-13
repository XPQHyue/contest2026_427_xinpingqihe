/****************************************************************************
 * apps/examples/phywear/pw_skill.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Install the embedded PhyWear Markdown Skills into the AI Agent's skill
 * directory.  See pw_skill.h for why this runs at every startup.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>

#include "pw_skill.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Must match CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR "/skills/" of the AI Agent
 * package: this is the directory the agent enumerates for its skill summary.
 */

#define PW_SKILLS_DIR     "/data/agent"
#define PW_SKILLS_SUBDIR  PW_SKILLS_DIR "/skills"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pw_skill_s
{
  FAR const char *name;    /* File name inside PW_SKILLS_SUBDIR */
  FAR const char *content; /* NUL terminated Markdown source */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pw_skill_s g_pw_skills[] =
{
  { "phywear-physics-coach.md", g_pw_skill_phywear_physics_coach },
};

#define PW_SKILL_COUNT (sizeof(g_pw_skills) / sizeof(g_pw_skills[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Return true when the file already holds exactly this content. */

static bool pw_skill_same(FAR const char *path, FAR const char *content)
{
  char buf[256];
  size_t want = strlen(content);
  size_t total = 0;
  FILE *f = fopen(path, "r");

  if (f == NULL)
    {
      return false;
    }

  while (total < want)
    {
      size_t n = fread(buf, 1, sizeof(buf), f);

      if (n == 0)
        {
          break;
        }

      if (memcmp(buf, content + total, n) != 0)
        {
          fclose(f);
          return false;
        }

      total += n;
    }

  /* Same bytes and nothing left over in the file. */

  bool same = (total == want) && (fgetc(f) == EOF);
  fclose(f);
  return same;
}

static int pw_skill_write(FAR const struct pw_skill_s *skill)
{
  char path[128];
  FILE *f;

  snprintf(path, sizeof(path), "%s/%s", PW_SKILLS_SUBDIR, skill->name);

  if (pw_skill_same(path, skill->content))
    {
      return 0;
    }

  f = fopen(path, "w");
  if (f == NULL)
    {
      syslog(LOG_ERR, "[phywear] cannot write skill %s: %d\n", path, errno);
      return -errno;
    }

  size_t len = strlen(skill->content);
  size_t n = fwrite(skill->content, 1, len, f);
  fclose(f);

  if (n != len)
    {
      syslog(LOG_ERR, "[phywear] short write on skill %s (%zu/%zu)\n",
             path, n, len);
      return -EIO;
    }

  syslog(LOG_INFO, "[phywear] installed skill %s (%zu bytes)\n", path, len);
  return 1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int pw_skill_install(void)
{
  int written = 0;

  if (mkdir(PW_SKILLS_DIR, 0755) != 0 && errno != EEXIST)
    {
      syslog(LOG_WARNING, "[phywear] mkdir %s failed: %d\n",
             PW_SKILLS_DIR, errno);
      return -errno;
    }

  if (mkdir(PW_SKILLS_SUBDIR, 0755) != 0 && errno != EEXIST)
    {
      syslog(LOG_WARNING, "[phywear] mkdir %s failed: %d\n",
             PW_SKILLS_SUBDIR, errno);
      return -errno;
    }

  for (size_t i = 0; i < PW_SKILL_COUNT; i++)
    {
      int rc = pw_skill_write(&g_pw_skills[i]);

      if (rc < 0)
        {
          return rc;
        }

      written += rc;
    }

  syslog(LOG_INFO, "[phywear] skills ready in %s (%d installed this boot)\n",
         PW_SKILLS_SUBDIR, written);
  return written;
}
