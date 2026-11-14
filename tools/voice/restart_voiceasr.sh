#!/bin/bash
# restart_voiceasr.sh —— 幂等重启 voiceasr（root）
set -u
P=$(ls -d /home/xpqh/.local/lib/python3.*/site-packages | head -1)
sudo -n pkill -f voiceasr.py 2>/dev/null
sleep 1
sudo -n cp /tmp/voiceasr.py /root/voiceasr.py
sudo -n bash -c "cd /root && PYTHONPATH=$P nohup python3 /root/voiceasr.py > /root/voiceasr.out 2>&1 < /dev/null &"
for i in $(seq 1 20); do
  if ss -ltn 2>/dev/null | grep -q '192.168.50.1:28790'; then echo "board port up (${i}s)"; break; fi
  sleep 1
done
ss -ltn 2>/dev/null | grep -E '2879[01]'
sudo -n grep -c 'ACK from board' /root/voiceasr.py