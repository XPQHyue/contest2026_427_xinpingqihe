#ifndef PW_RELAY_H
#define PW_RELAY_H

/* 发一句话给 VM 侧 relayd/voiceasr，等待一句回答；成功返回 0，答案由 pw_ai_note 上屏。 */
int pw_relay_ask(const char *text);

/* 起一个后台线程订阅 VM 推送（语音输入的识别结果与回答会主动上屏）。 */
void pw_relay_sub_start(void);

#endif /* PW_RELAY_H */