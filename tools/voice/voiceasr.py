#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""voiceasr.py —— VM 侧语音入口（28791）+ 板子订阅通道

链路：
  浏览器/手机（录音） —POST /asr--> 本服务 -> ffmpeg 16k mono -> vosk 中文识别
      -> 文本 -> LLM（同 relayd 的 key/model） -> 回答
      -> 推给板子（板子用 pw_relay 订阅模式连 28790，收 `A <text>` 行）

协议（TCP 28790，板子侧）：
  板子连上来先发 `S\n`（订阅）或 `Q <text>\n`（同步问答）。
  订阅连接上，本服务主动下发：
      `A <answer>\n`   回答（板子 pw_ai_note 上屏）
      `X <heard>\n`    识别到的原话（可选展示）
"""

import io
import json
import os
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.request

MODEL_DIR = "/opt/vosk/vosk-model-small-cn-0.22"
URL = "https://maas.qianwenaiapi.com/compatible-mode/v1/chat/completions"
MODEL = "deepseek-v4.1-flash-1017"
SYS = ("你是 PhyWear 智能手表上的语音助手。用户是在手表上对你说话，可能有识别误差。"
       "回答要短：不超过 60 个汉字，口语化，不要 markdown，不要列表。")

LOG = "/root/voiceasr.log"
_sub_lock = threading.Lock()
_subs = []          # 已订阅的板子连接（socket 对象）


def log(msg):
    line = "%s %s" % (time.strftime("%H:%M:%S"), msg)
    print(line, flush=True)
    try:
        with io.open(LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


def api_key():
    key = os.environ.get("QW_API_KEY", "").strip()
    if key:
        return key
    try:
        import yaml  # noqa
        with io.open("/home/xpqh/.dsh/.credentials.yaml", encoding="utf-8") as f:
            for line in f:
                if "QW_API_KEY" in line:
                    return line.split(":", 1)[1].strip().strip('"\'')
    except Exception:
        pass
    return ""


def ask_llm(text):
    key = api_key()
    if not key:
        return "（VM 侧缺少 API key）"
    body = json.dumps({
        "model": MODEL,
        "messages": [{"role": "system", "content": SYS},
                     {"role": "user", "content": text}],
        "temperature": 0.6,
        "max_tokens": 200,
    }).encode()
    req = urllib.request.Request(URL, data=body, method="POST", headers={
        "Content-Type": "application/json",
        "Authorization": "Bearer " + key,
    })
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=40) as r:
            obj = json.loads(r.read().decode("utf-8", "replace"))
        ans = obj["choices"][0]["message"]["content"].strip()
        log("LLM ok %.1fs: %s" % (time.time() - t0, ans[:60]))
        return ans
    except Exception as e:
        log("LLM fail: %r" % (e,))
        return "（云端没回话，稍后再试）"


def asr_wav(path):
    import vosk
    vosk.SetLogLevel(-1)
    model = MODEL_CACHE.get("m")
    if model is None:
        model = vosk.Model(MODEL_DIR)
        MODEL_CACHE["m"] = model
    rec = vosk.KaldiRecognizer(model, 16000)
    with io.open(path, "rb") as f:
        f.seek(44)
        while True:
            data = f.read(4096)
            if not data:
                break
            rec.AcceptWaveform(data)
    res = json.loads(rec.FinalResult())
    return res.get("text", "").replace(" ", "")


MODEL_CACHE = {}


def push_to_boards(line):
    with _sub_lock:
        dead = []
        for s in _subs:
            try:
                s.sendall((line + "\n").encode())
            except Exception:
                dead.append(s)
        for s in dead:
            try:
                s.close()
            except Exception:
                pass
            _subs.remove(s)


def handle_board(conn, addr):
    log("board connected %r" % (addr,))
    buf = b""
    try:
        while True:
            d = conn.recv(1024)
            if not d:
                break
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.decode("utf-8", "replace").strip()
                if not line:
                    continue
                if line.startswith("S"):
                    with _sub_lock:
                        _subs.append(conn)
                    log("board subscribed")
                    conn.sendall(("A 我在，说话吧\n").encode())
                elif line.startswith("Q "):
                    q = line[2:]
                    ans = ask_llm(q)
                    conn.sendall(("A " + ans + "\n").encode())
                elif line.startswith("K"):
                    log("ACK from board (displayed)")
                elif line.startswith("P"):
                    conn.sendall(("A 在线\n").encode())
    except Exception as e:
        log("board err %r" % (e,))
    finally:
        with _sub_lock:
            if conn in _subs:
                _subs.remove(conn)
        try:
            conn.close()
        except Exception:
            pass


def board_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("192.168.50.1", 28790))
    srv.listen(4)
    log("board server on 192.168.50.1:28790")
    while True:
        c, a = srv.accept()
        threading.Thread(target=handle_board, args=(c, a), daemon=True).start()


PAGE = """<!doctype html><html lang="zh"><meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1">
<title>PhyWear 语音</title>
<style>
body{background:#0a0f1e;color:#dbe7f3;font-family:system-ui,"Microsoft YaHei";margin:0;
display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;gap:22px}
h1{font-size:20px;font-weight:600;letter-spacing:.04em;color:#22d3ee}
#b{width:190px;height:190px;border-radius:50%;border:2px solid #22d3ee;background:#0f1b30;
color:#22d3ee;font-size:19px;cursor:pointer;transition:.15s}
#b.on{background:#22d3ee;color:#04121f;transform:scale(.96)}
#t{min-height:60px;max-width:min(680px,88vw);text-align:center;font-size:17px;line-height:1.6}
#a{color:#34d399;font-weight:600}
small{color:#6b8299}
</style>
<h1>PhyWear · 按住说话</h1>
<button id=b>按住说话</button>
<div id=t><div id=q></div><div id=a></div></div>
<small id=s>点住按钮说话，松手后自动识别并回答（回答也会显示在手表上）</small>
<script>
let rec,chunks=[],on=false;
const b=document.getElementById('b'),q=document.getElementById('q'),
      a=document.getElementById('a'),s=document.getElementById('s');
async function start(){
  on=true;b.classList.add('on');b.textContent='松手结束';q.textContent='';a.textContent='';
  const st=await navigator.mediaDevices.getUserMedia({audio:true});
  rec=new MediaRecorder(st,{mimeType:'audio/webm;codecs=opus'});chunks=[];
  rec.ondataavailable=e=>chunks.push(e.data);
  rec.onstop=async()=>{
    st.getTracks().forEach(t=>t.stop());
    s.textContent='识别中…';
    const blob=new Blob(chunks,{type:'audio/webm'});
    const fd=new FormData();fd.append('f',blob,'a.webm');
    try{
      const r=await fetch('/asr',{method:'POST',body:fd});
      const j=await r.json();
      q.textContent=j.text?('你说：'+j.text):'（没听清）';
      a.textContent=j.answer||'';
    }catch(e){s.textContent='请求失败：'+e}
    s.textContent=j_ok();
    b.classList.remove('on');b.textContent='按住说话';on=false;
  };
  rec.start();
}
function j_ok(){return '完成。回答同时已推到手表（若已连接）。'}
b.addEventListener('pointerdown',e=>{e.preventDefault();if(!on)start()});
b.addEventListener('pointerup',e=>{e.preventDefault();if(rec&&rec.state==='recording')rec.stop()});
b.addEventListener('pointerleave',e=>{if(rec&&rec.state==='recording')rec.stop()});
</script></html>"""


def http_server():
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    import cgi

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            b = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def do_POST(self):
            n = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(n)
            ctype = self.headers.get("Content-Type", "")
            data = None
            if ctype.startswith("multipart/form-data"):
                boundary = ctype.split("boundary=")[1].encode()
                for part in raw.split(b"--" + boundary):
                    if b"Content-Disposition" in part and b"filename" in part:
                        data = part.split(b"\r\n\r\n", 1)[1].rsplit(b"\r\n", 1)[0]
                        break
            else:
                data = raw
            if not data:
                return self._json({"text": "", "answer": "（没收到音频）"})
            io.open("/tmp/voice_in.bin", "wb").write(data)
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", "/tmp/voice_in.bin",
                            "-ar", "16000", "-ac", "1", "-f", "wav", "/tmp/voice_in.wav"],
                           capture_output=True)
            text = ""
            try:
                text = asr_wav("/tmp/voice_in.wav")
            except Exception as e:
                log("asr err %r" % (e,))
            log("heard: %r" % (text,))
            if not text:
                return self._json({"text": "", "answer": "（没听清，再说一次？）"})
            push_to_boards("X " + text)
            ans = ask_llm(text)
            push_to_boards("A " + ans)
            self._json({"text": text, "answer": ans})

        def _json(self, obj):
            b = json.dumps(obj, ensure_ascii=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

    srv = ThreadingHTTPServer(("0.0.0.0", 28791), H)
    log("http on :28791")
    srv.serve_forever()


if __name__ == "__main__":
    threading.Thread(target=board_server, daemon=True).start()
    http_server()