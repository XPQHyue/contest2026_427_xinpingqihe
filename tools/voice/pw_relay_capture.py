#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""capture.py —— 常开串口采集（复位一次），持续写 /root/vlog.txt"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/root/vlog.txt"

ser = serial.Serial(PORT, 1000000, timeout=0.3)
ser.reset_input_buffer()
ser.dtr = False
ser.rts = False
time.sleep(0.15)
ser.rts = True
time.sleep(0.20)
ser.rts = False
ser.dtr = False

f = open(OUT, "wb", buffering=0)
t0 = time.time()
while time.time() - t0 < 600:
    d = ser.read(4096)
    if d:
        f.write(d)