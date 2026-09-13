#!/usr/bin/env python3
"""NSH/BASH 双模终端 - 黄山派 SF32LB52 专用 v4"""
import serial
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox
import re
import time
import os
import subprocess
import pty
import select
import fcntl
import termios
import struct
import json

BAUD = 1000000
PORT = '/dev/ttyUSB0'
LOG_DIR = os.path.expanduser('~/nsh_logs')
CONFIG_FILE = os.path.expanduser('~/.nsh_terminal.json')

class NSHTerminal:
    def __init__(self):
        self.ser = None
        self.running = False
        self.history = []
        self.history_idx = -1
        self.logging = False
        self.log_file = None
        self.bytes_rx = 0
        self.bytes_tx = 0
        self.mode = 'NSH'  # NSH 或 BASH
        self.bash_pty = None
        self.bash_pid = None
        self.auto_connect = True
        self.load_config()
        self.setup_gui()
        if self.auto_connect:
            self.connect()
        else:
            self.status_label.config(text='● 未连接', fg='#888888')
            self.bottom_status.config(text='自动连接已关闭 | 点击"连接"手动连接串口')

    def load_config(self):
        """加载配置"""
        try:
            if os.path.exists(CONFIG_FILE):
                with open(CONFIG_FILE, 'r') as f:
                    cfg = json.load(f)
                    self.auto_connect = cfg.get('auto_connect', True)
        except:
            self.auto_connect = True

    def save_config(self):
        """保存配置"""
        try:
            cfg = {'auto_connect': self.auto_connect}
            with open(CONFIG_FILE, 'w') as f:
                json.dump(cfg, f)
        except:
            pass

    def setup_gui(self):
        self.root = tk.Tk()
        self.root.title('NSH Terminal - 黄山派 SF32LB52')
        self.root.geometry('850x550')
        self.root.configure(bg='#1e1e1e')
        self.root.protocol('WM_DELETE_WINDOW', self.on_close)

        # 顶部工具栏
        toolbar = tk.Frame(self.root, bg='#2d2d2d', height=36)
        toolbar.pack(fill=tk.X, padx=0, pady=0)
        toolbar.pack_propagate(False)

        btn_style = {'font': ('Consolas', 10), 'bg': '#3c3c3c', 'fg': '#cccccc',
                     'relief': tk.FLAT, 'padx':8, 'pady':2, 'cursor': 'hand2'}

        # 模式切换按钮
        self.mode_btn = tk.Button(
            toolbar, text='⟳ 切换 BASH', command=self.toggle_mode,
            font=('Consolas', 10, 'bold'), bg='#0e639c', fg='#ffffff',
            relief=tk.FLAT, padx=10, pady=2, cursor='hand2'
        )
        self.mode_btn.pack(side=tk.LEFT, padx=5, pady=3)

        # 连接/断开按钮
        self.connect_btn = tk.Button(
            toolbar, text='🔌 连接', command=self.toggle_connect,
            font=('Consolas', 10, 'bold'), bg='#2e7d32', fg='#ffffff',
            relief=tk.FLAT, padx=10, pady=2, cursor='hand2'
        )
        self.connect_btn.pack(side=tk.LEFT, padx=5, pady=3)

        # 烧录按钮
        self.flash_btn = tk.Button(
            toolbar, text='⚡ 烧录', command=self.flash_firmware,
            font=('Consolas', 10, 'bold'), bg='#e65100', fg='#ffffff',
            relief=tk.FLAT, padx=10, pady=2, cursor='hand2'
        )
        self.flash_btn.pack(side=tk.LEFT, padx=5, pady=3)

        # 烧录配置选择
        self.flash_config_var = tk.StringVar(value='nsh_phywear')
        flash_configs = ['nsh_phywear', 'nsh_lvgl', 'nsh_minimal', 'nsh']
        self.flash_config_menu = tk.OptionMenu(
            toolbar, self.flash_config_var, *flash_configs
        )
        self.flash_config_menu.config(
            font=('Consolas', 9), bg='#3c3c3c', fg='#cccccc',
            relief=tk.FLAT, highlightthickness=0, cursor='hand2'
        )
        self.flash_config_menu['menu'].config(
            font=('Consolas', 9), bg='#3c3c3c', fg='#cccccc'
        )
        self.flash_config_menu.pack(side=tk.LEFT, padx=2, pady=3)

        tk.Button(toolbar, text='清屏', command=self.clear_output, **btn_style).pack(side=tk.LEFT, padx=2, pady=3)
        tk.Button(toolbar, text='复位板子', command=self.reset_board, **btn_style).pack(side=tk.LEFT, padx=2, pady=3)
        tk.Button(toolbar, text='Ctrl+C', command=self.send_ctrl_c, **btn_style).pack(side=tk.LEFT, padx=2, pady=3)

        self.log_btn = tk.Button(toolbar, text='开始记录', command=self.toggle_log, **btn_style)
        self.log_btn.pack(side=tk.LEFT, padx=2, pady=3)

        # 快捷命令按钮
        self.quick_btns = []
        for cmd in ['ls', 'free', 'ps', 'help', 'uname -a']:
            b = tk.Button(toolbar, text=cmd, command=lambda c=cmd: self.quick_cmd(c),
                          font=('Consolas', 10), bg='#0e639c', fg='#ffffff',
                          relief=tk.FLAT, padx=8, pady=2, cursor='hand2')
            b.pack(side=tk.LEFT, padx=2, pady=3)
            self.quick_btns.append(b)

        # 自动连接复选框（右侧）
        self.auto_connect_var = tk.BooleanVar(value=self.auto_connect)
        self.auto_connect_cb = tk.Checkbutton(
            toolbar, text='自动连接', variable=self.auto_connect_var,
            command=self.on_auto_connect_toggle,
            font=('Consolas', 10), bg='#2d2d2d', fg='#cccccc',
            selectcolor='#3c3c3c', activebackground='#2d2d2d',
            activeforeground='#ffffff', cursor='hand2'
        )
        self.auto_connect_cb.pack(side=tk.RIGHT, padx=5, pady=3)

        # 状态指示
        self.status_label = tk.Label(toolbar, text='● 连接中...', font=('Consolas', 10),
                                     bg='#2d2d2d', fg='#aaaaaa')
        self.status_label.pack(side=tk.RIGHT, padx=10)

        self.stats_label = tk.Label(toolbar, text='RX:0 TX:0', font=('Consolas', 9),
                                    bg='#2d2d2d', fg='#666666')
        self.stats_label.pack(side=tk.RIGHT, padx=5)

        # 输出区
        self.output = scrolledtext.ScrolledText(
            self.root, wrap=tk.WORD, font=('Consolas', 12),
            bg='#1e1e1e', fg='#00ff00', insertbackground='#00ff00',
            state='disabled', height=25, padx=8, pady=5
        )
        self.output.pack(fill=tk.BOTH, expand=True, padx=5, pady=(5,0))
        self.output.bind('<Control-c>', lambda e: self.root.event_generate('<<Copy>>'))

        # 底部输入栏
        frame = tk.Frame(self.root, bg='#1e1e1e')
        frame.pack(fill=tk.X, padx=5, pady=5)

        self.prompt_label = tk.Label(
            frame, text='nsh>', font=('Consolas', 12, 'bold'),
            bg='#1e1e1e', fg='#00ff00'
        )
        self.prompt_label.pack(side=tk.LEFT)

        self.input_entry = tk.Entry(
            frame, font=('Consolas', 12),
            bg='#2d2d2d', fg='#ffffff', insertbackground='#ffffff',
            relief=tk.FLAT, bd=5
        )
        self.input_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(5,0))
        self.input_entry.bind('<Return>', self.send_command)
        self.input_entry.bind('<Up>', self.history_up)
        self.input_entry.bind('<Down>', self.history_down)
        self.input_entry.bind('<Control-l>', lambda e: self.clear_output())
        self.input_entry.bind('<Control-c>', lambda e: self.send_ctrl_c())
        self.input_entry.focus_set()

        # 底部状态栏
        self.bottom_status = tk.Label(
            self.root, text='就绪 | 上下键翻历史 | Ctrl+C 中断 | Ctrl+L 清屏',
            font=('Consolas', 9), bg='#252526', fg='#555555', anchor=tk.W, padx=10
        )
        self.bottom_status.pack(fill=tk.X, side=tk.BOTTOM)

    def on_auto_connect_toggle(self):
        """自动连接开关变化"""
        self.auto_connect = self.auto_connect_var.get()
        self.save_config()
        if self.auto_connect:
            self.append_output('\n--- 自动连接已开启 ---\n')
        else:
            self.append_output('\n--- 自动连接已关闭，下次启动需手动连接 ---\n')

    def toggle_connect(self):
        """手动连接/断开"""
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def disconnect(self):
        """断开连接"""
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None
        self.status_label.config(text='● 已断开', fg='#888888')
        self.connect_btn.config(text='🔌 连接', bg='#2e7d32')
        self.append_output('\n--- 已断开连接 ---\n')

    def connect(self):
        try:
            self.ser = serial.Serial(PORT, BAUD, timeout=0.1)
            self.ser.rts = False
            self.ser.dtr = False
            self.running = True
            self.status_label.config(text='● NSH 已连接', fg='#00ff00')
            self.connect_btn.config(text='🔌 断开', bg='#c33c3c')
            self.bottom_status.config(text=f'NSH 模式 | {PORT} @ {BAUD} | 上下键翻历史 | Ctrl+C 中断 | Ctrl+L 清屏')
            self.reader_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.reader_thread.start()
            self.update_stats()
        except Exception as e:
            self.status_label.config(text='● 连接失败', fg='#ff4444')
            self.connect_btn.config(text='🔌 连接', bg='#2e7d32')
            if self.auto_connect:
                self.append_output(f'\n--- 自动连接失败: {e}，板子未就绪 ---\n')
            else:
                messagebox.showerror('连接错误', f'无法打开 {PORT}:\n{e}')

    def update_stats(self):
        if self.running:
            self.stats_label.config(text=f'RX:{self.bytes_rx} TX:{self.bytes_tx}')
            self.root.after(1000, self.update_stats)

    def read_serial(self):
        while self.running and self.mode == 'NSH':
            try:
                data = self.ser.read(4096)
                if data:
                    self.bytes_rx += len(data)
                    text = data.decode(errors='replace')
                    text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)
                    text = text.replace('\r\n', '\n').replace('\r', '\n')
                    if self.logging and self.log_file:
                        self.log_file.write(text)
                        self.log_file.flush()
                    self.root.after(0, self.append_output, text)
            except:
                if self.running:
                    break

    def read_bash(self):
        """读取 BASH 输出"""
        while self.running and self.mode == 'BASH':
            try:
                if self.bash_pty:
                    r, _, _ = select.select([self.bash_pty], [], [], 0.1)
                    if r:
                        data = os.read(self.bash_pty, 4096)
                        if data:
                            text = data.decode(errors='replace')
                            if self.logging and self.log_file:
                                self.log_file.write(text)
                                self.log_file.flush()
                            self.root.after(0, self.append_output, text)
                        else:
                            break
            except:
                if self.running and self.mode == 'BASH':
                    break

    def append_output(self, text):
        self.output.config(state='normal')
        self.output.insert(tk.END, text)
        self.output.see(tk.END)
        self.output.config(state='disabled')

    def toggle_mode(self):
        if self.mode == 'NSH':
            self.switch_to_bash()
        else:
            self.switch_to_nsh()

    def switch_to_bash(self):
        """切换到 BASH 模式"""
        self.mode = 'BASH'

        # 启动本地 BASH
        master, slave = pty.openpty()
        self.bash_pty = master
        self.bash_pid = os.fork()
        if self.bash_pid == 0:
            os.setsid()
            os.close(master)
            winsize = struct.pack('HHHH', 24, 80, 0, 0)
            fcntl.ioctl(slave, termios.TIOCSWINSZ, winsize)
            os.dup2(slave, 0)
            os.dup2(slave, 1)
            os.dup2(slave, 2)
            os.close(slave)
            os.execvp('bash', ['bash', '--login'])
        else:
            os.close(slave)

        self.mode_btn.config(text='⟳ 切换 NSH', bg='#c33c3c')
        self.prompt_label.config(text='bash$', fg='#ffaa00')
        self.status_label.config(text='● BASH 模式', fg='#ffaa00')
        self.bottom_status.config(text='BASH 本地模式 | 输入命令执行 | 点击"切换 NSH"回到串口终端')

        for b, cmd in zip(self.quick_btns, ['ls', 'pwd', 'whoami', 'df -h', 'uname -a']):
            b.config(command=lambda c=cmd: self.quick_cmd(c), text=cmd)

        self.bash_thread = threading.Thread(target=self.read_bash, daemon=True)
        self.bash_thread.start()

        self.append_output('\n--- 已切换到 BASH 本地终端 ---\n')

    def switch_to_nsh(self):
        """切换回 NSH 模式"""
        if self.bash_pid:
            try:
                os.kill(self.bash_pid, 9)
                os.waitpid(self.bash_pid, 0)
            except:
                pass
            self.bash_pid = None
        if self.bash_pty:
            try:
                os.close(self.bash_pty)
            except:
                pass
            self.bash_pty = None

        self.mode = 'NSH'

        if not self.ser or not self.ser.is_open:
            self.connect()
        else:
            self.reader_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.reader_thread.start()

        self.mode_btn.config(text='⟳ 切换 BASH', bg='#0e639c')
        self.prompt_label.config(text='nsh>', fg='#00ff00')
        self.status_label.config(text='● NSH 已连接', fg='#00ff00')
        self.bottom_status.config(text=f'NSH 模式 | {PORT} @ {BAUD} | 上下键翻历史 | Ctrl+C 中断 | Ctrl+L 清屏')

        for b, cmd in zip(self.quick_btns, ['ls', 'free', 'ps', 'help', 'uname -a']):
            b.config(command=lambda c=cmd: self.quick_cmd(c), text=cmd)

        self.append_output('\n--- 已切换回 NSH 串口终端 ---\n')

        if self.ser and self.ser.is_open:
            self.ser.write(b'\r\n')

    def send_command(self, event=None):
        cmd = self.input_entry.get().strip()
        if not cmd:
            return
        self.history.append(cmd)
        self.history_idx = len(self.history)

        if self.mode == 'NSH':
            if self.ser and self.ser.is_open:
                msg = (cmd + '\r\n').encode()
                self.ser.write(msg)
                self.bytes_tx += len(msg)
            else:
                self.append_output('--- 未连接串口 ---\n')
        elif self.mode == 'BASH':
            if self.bash_pty:
                os.write(self.bash_pty, (cmd + '\n').encode())

        self.input_entry.delete(0, tk.END)

    def quick_cmd(self, cmd):
        self.input_entry.delete(0, tk.END)
        self.input_entry.insert(0, cmd)
        self.send_command()

    def send_ctrl_c(self):
        if self.mode == 'NSH':
            if self.ser and self.ser.is_open:
                self.ser.write(b'\x03')
                self.bytes_tx += 1
        elif self.mode == 'BASH':
            if self.bash_pty:
                os.write(self.bash_pty, b'\x03')

    def reset_board(self):
        if self.mode == 'NSH' and self.ser and self.ser.is_open:
            self.ser.rts = True
            time.sleep(0.05)
            self.ser.rts = False
            self.append_output('\n--- 板子已复位 ---\n')

    def flash_firmware(self):
        """编译并烧录固件"""
        config = self.flash_config_var.get()
        self.append_output(f'\n--- 开始编译烧录: {config} ---\n')
        self.flash_btn.config(state='disabled', text='⏳ 烧录中...')

        # 先断开串口
        if self.ser and self.ser.is_open:
            self.disconnect()

        def flash_thread():
            script = os.path.expanduser('~/openvela/build_and_flash.sh')
            try:
                proc = subprocess.Popen(
                    ['bash', script, config],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1
                )
                for line in proc.stdout:
                    self.root.after(0, self.append_output, line)
                proc.wait()
                if proc.returncode == 0:
                    self.root.after(0, self.append_output, '\n--- 烧录成功 ---\n')
                else:
                    self.root.after(0, self.append_output, f'\n--- 烧录失败 (返回码: {proc.returncode}) ---\n')
            except Exception as e:
                self.root.after(0, self.append_output, f'\n--- 烧录错误: {e} ---\n')
            finally:
                self.root.after(0, self.flash_btn.config, {'state': 'normal', 'text': '⚡ 烧录'})
                # 烧录完成后自动重连
                time.sleep(1)
                self.root.after(0, self.connect)

        threading.Thread(target=flash_thread, daemon=True).start()

    def clear_output(self):
        self.output.config(state='normal')
        self.output.delete(1.0, tk.END)
        self.output.config(state='disabled')

    def toggle_log(self):
        if not self.logging:
            os.makedirs(LOG_DIR, exist_ok=True)
            fname = time.strftime('nsh_%Y%m%d_%H%M%S.log')
            self.log_file = open(os.path.join(LOG_DIR, fname), 'w')
            self.logging = True
            self.log_btn.config(text='停止记录', bg='#c33c3c')
            self.append_output(f'\n--- 日志开始: {fname} ---\n')
        else:
            self.logging = False
            if self.log_file:
                self.log_file.close()
                self.log_file = None
            self.log_btn.config(text='开始记录', bg='#3c3c3c')
            self.append_output('\n--- 日志已保存 ---\n')

    def history_up(self, event=None):
        if self.history and self.history_idx > 0:
            self.history_idx -= 1
            self.input_entry.delete(0, tk.END)
            self.input_entry.insert(0, self.history[self.history_idx])
        return 'break'

    def history_down(self, event=None):
        if self.history_idx < len(self.history) - 1:
            self.history_idx += 1
            self.input_entry.delete(0, tk.END)
            self.input_entry.insert(0, self.history[self.history_idx])
        else:
            self.history_idx = len(self.history)
            self.input_entry.delete(0, tk.END)
        return 'break'

    def on_close(self):
        self.running = False
        if self.bash_pid:
            try:
                os.kill(self.bash_pid, 9)
            except:
                pass
        if self.log_file:
            self.log_file.close()
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()

    def run(self):
        self.root.mainloop()

if __name__ == '__main__':
    app = NSHTerminal()
    app.run()
