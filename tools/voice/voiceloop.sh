#!/bin/bash
# voiceloop2.sh —— 串口常开（pan_ask5）+ 语音推送的完整回合
set -u
OUT=/tmp/oV3.txt
WAV=${1:-/tmp/q.wav}
LOG=/root/voiceasr.log

rm -f $OUT
nohup env WAIT=140 python3 /tmp/pan_ask5.py 'phywear status' > $OUT 2>&1 &
PID=$!

clean() { sed 's/\x1b\[[0-9;]*m//g' "$OUT"; }

echo "== 等订阅"
OK=0
for i in $(seq 1 120); do
  if clean | grep -q "\[relay\] subscribed"; then OK=1; echo "subscribed at ${i}s"; break; fi
  sleep 1
done
if [ "$OK" = "0" ]; then
  echo "-- 失败，尾部输出："
  clean | tail -20
  exit 2
fi

sleep 3
echo "== 推语音"
curl -s -m 90 -F "f=@$WAV" http://127.0.0.1:28791/asr | head -c 400; echo

echo "== 等板子上屏（板子日志里的 push 行）"
for i in $(seq 1 25); do
  if clean | grep -q "\[relay\] push:"; then echo "pushed at ${i}s"; break; fi
  sleep 1
done

kill $PID 2>/dev/null
sleep 1
echo "== 板子侧记录"
clean | grep -aE "\[relay\]|ai-coach|Network connected" | tail -12
echo "== 服务端记录"
sudo -n tail -6 $LOG