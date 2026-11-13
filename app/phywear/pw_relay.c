/* pw_relay.c —— 端云中继客户端（订阅 + 同步问答）
 *
 * 设计缘由：设备直连云端时，TLS + 16 KB 请求体在 400 B MTU 的蓝牙 PAN 上
 * 会卡死在 TCP 发送窗口（mbedtls_ssl_write 直接阻塞）。所以设备只做瘦客户端：
 * 文本走 192.168.50.1:28790 的一行协议，HTTPS/LLM 全在 VM 侧。
 *
 * 协议：`Q <text>` 同步问答（回 `A <text>` / `E <why>`）
 *       `S`        订阅：VM 侧有语音输入时主动下发 `A <text>` / `X <heard>`
 */

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pw_relay.h"
#include "pw_ai.h"

#define RELAY_HOST "192.168.50.1"
#define RELAY_BUF  384

extern bool network_is_connected(void);

static int relay_conn(void)
{
  struct sockaddr_in sa;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd < 0)
    {
      return -1;
    }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(28790);
  sa.sin_addr.s_addr = inet_addr(RELAY_HOST);

  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
      close(fd);
      return -1;
    }

  return fd;
}

/* 收一行，成功后把内容（去掉行尾）放到 buf */
static int relay_line(int fd, char *buf, size_t len)
{
  size_t used = 0;

  for (;;)
    {
      ssize_t n = recv(fd, buf + used, len - 1 - used, 0);

      if (n <= 0)
        {
          return -1;
        }

      used += (size_t)n;
      buf[used] = '\0';

      char *nl = strchr(buf, '\n');
      if (nl != NULL)
        {
          *nl = '\0';
          return 0;
        }

      if (used >= len - 1)
        {
          buf[len - 1] = '\0';
          return 0;
        }
    }
}

int pw_relay_ask(const char *text)
{
  char buf[RELAY_BUF];
  int fd;
  int len;

  if (text == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  fd = -1;
  for (int i = 0; i < 10 && fd < 0; i++)
    {
      if (network_is_connected())
        {
          fd = relay_conn();
        }

      if (fd < 0)
        {
          sleep(1);
        }
    }

  if (fd < 0)
    {
      printf("[relay] no link\n");
      return -ENOTCONN;
    }

  len = snprintf(buf, sizeof(buf), "Q %s\n", text);
  if (len <= 0 || len >= (int)sizeof(buf))
    {
      close(fd);
      return -E2BIG;
    }

  if (send(fd, buf, (size_t)len, 0) != len)
    {
      close(fd);
      return -EIO;
    }

  if (relay_line(fd, buf, sizeof(buf)) < 0)
    {
      close(fd);
      return -EIO;
    }

  close(fd);

  if (buf[0] == 'A')
    {
      pw_ai_note(buf[1] ? buf + 2 : "");
      printf("[relay] A: %s\n", buf + 2);
      return 0;
    }

  printf("[relay] E: %s\n", buf + 2);
  return -EIO;
}

/* ---- 订阅线程：VM 侧语音输入的答案主动推上来 ---- */

static void *relay_sub_thread(void *arg)
{
  char buf[RELAY_BUF];

  (void)arg;

  for (;;)
    {
      int fd;

      if (!network_is_connected())
        {
          sleep(2);
          continue;
        }

      fd = relay_conn();
      if (fd < 0)
        {
          sleep(3);
          continue;
        }

      if (send(fd, "S\n", 2, 0) != 2)
        {
          close(fd);
          sleep(3);
          continue;
        }

      printf("[relay] subscribed\n");

      while (relay_line(fd, buf, sizeof(buf)) == 0)
        {
          if (buf[0] == 'A')
            {
              pw_ai_note(buf[1] ? buf + 2 : "");
              printf("[relay] push: %s\n", buf + 2);
            }
          else if (buf[0] == 'X')
            {
              printf("[relay] heard: %s\n", buf + 2);
            }
        }

      close(fd);
      printf("[relay] link lost, retry\n");
      sleep(3);
    }

  return NULL;
}

void pw_relay_sub_start(void)
{
  pthread_t tid;

  if (pthread_create(&tid, NULL, relay_sub_thread, NULL) == 0)
    {
      pthread_detach(tid);
      printf("[relay] subscribe thread started\n");
    }
  else
    {
      printf("[relay] subscribe thread create failed\n");
    }
}