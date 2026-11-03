"""Full-screen on-screen run driven by real input (only with the player's go-ahead,
while they are away from the PC).

Starts the game in C2HD_HEADLESS=3 (full screen, test window class, muted) and
runs a list of steps with real mouse and keyboard input (SendInput). Every
input first checks that the game's own window is the foreground window; if
anything else is in front, the run stops without sending anything.

usage: python -E -P tools/screendrive.py <game dir> <out dir> <steps file>

Windows only lets the process that sent the last input bring a window to the
front, so the run nudges the mouse by a pixel and then brings the game's
window forward ("front"). While the game is in front a pixel nudge every 45 s
keeps the screensaver away.

Steps, one per line ("#" comments):
  front                    bring the game's window to the front (once it exists)
  wait <s>                 sleep
  move <x> <y>             cursor to a screen pixel (absolute), then log where it really is
  menu <x> <y>             cursor to a point in 640x480 menu space (16:9 wide frame, centred)
  click                    left click
  key <name> [ms]          press a key (RETURN, ESCAPE, F4, UP ... or a letter or digit)
  altf4                    Alt+F4
  shot <name>              screenshot of the primary screen to <out dir>/<name>.png
  expect_exit <s>          wait up to s seconds for the game to end; log how long it took
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import threading
import time

from PIL import ImageGrab

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except OSError:
    user32.SetProcessDPIAware()

game, out, steps_file = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
VK = {"RETURN": 0x0D, "ESCAPE": 0x1B, "F4": 0x73, "MENU": 0x12, "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25,
      "RIGHT": 0x27, "SPACE": 0x20, "F1": 0x70}


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def send(*inputs):
    arr = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), arr, ctypes.sizeof(INPUT))


def log(msg):
    line = f"{time.time() - start:6.1f}  {msg}"
    print(line, flush=True)
    with open(os.path.join(out, "drive.log"), "a") as f:
        f.write(line + "\n")


def game_in_front():
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value == proc.pid


def guard():
    if proc.poll() is not None:
        log("the game has ended; stopping")
        sys.exit(1)
    if not game_in_front():
        log("the game is not the foreground window; stopping without sending input")
        proc.terminate()
        sys.exit(2)


def nudge():
    for d in (1, -1):
        i = INPUT(type=0)
        i.mi = MOUSEINPUT(d, 0, 0, 0x0001, 0, 0)  # relative MOVE
        send(i)
        time.sleep(0.05)


def game_window():
    found = []
    cb_t = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(h, _):
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(h, cls, 64)
        if pid.value == proc.pid and cls.value.startswith("Carma2MainWndClass"):
            found.append(h)
        return True
    user32.EnumWindows(cb_t(cb), 0)
    return found[0] if found else None


def front():
    for _ in range(100):
        h = game_window()
        if h:
            break
        time.sleep(0.2)
    else:
        log("no game window")
        sys.exit(3)
    for _ in range(5):
        nudge()
        user32.SetForegroundWindow(h)
        time.sleep(0.5)
        if game_in_front():
            log("game window in front")
            return
    log("could not bring the game window to the front")
    proc.terminate()
    sys.exit(2)


def keep_awake():
    while proc.poll() is None:
        time.sleep(45)
        if proc.poll() is None and game_in_front():
            nudge()


def cursor():
    p = wt.POINT()
    user32.GetCursorPos(ctypes.byref(p))
    return p.x, p.y


def move(x, y):
    guard()
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)  # virtual screen origin
    vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    nx = int((x - vx) * 65535 / (vw - 1))
    ny = int((y - vy) * 65535 / (vh - 1))
    mi = INPUT(type=0)
    mi.mi = MOUSEINPUT(nx, ny, 0, 0x0001 | 0x8000 | 0x4000, 0, 0)  # MOVE | ABSOLUTE | VIRTUALDESK
    send(mi)
    time.sleep(0.15)
    log(f"move to {x},{y}: cursor at {cursor()}")


def click():
    guard()
    down, up = INPUT(type=0), INPUT(type=0)
    down.mi = MOUSEINPUT(0, 0, 0, 0x0002, 0, 0)
    up.mi = MOUSEINPUT(0, 0, 0, 0x0004, 0, 0)
    send(down)
    time.sleep(0.08)
    send(up)
    log(f"click at {cursor()}")


def key(vk, ms=120, alt=False):
    guard()
    seq = []
    if alt:
        seq.append((VK["MENU"], 0))
    seq.append((vk, 0))
    for k, _ in seq:
        i = INPUT(type=1)
        i.ki = KEYBDINPUT(k, user32.MapVirtualKeyW(k, 0), 0, 0, 0)
        send(i)
        time.sleep(0.03)
    time.sleep(ms / 1000)
    for k, _ in reversed(seq):
        i = INPUT(type=1)
        i.ki = KEYBDINPUT(k, user32.MapVirtualKeyW(k, 0), 0x0002, 0, 0)
        send(i)
        time.sleep(0.03)


sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)  # primary screen
env = dict(os.environ, C2HD_HEADLESS="3", C2HD_QUIT=os.environ.get("C2HD_QUIT", "180"))
start = time.time()
open(os.path.join(out, "drive.log"), "w").close()
proc = subprocess.Popen([os.path.join(game, "CARMA2_HW.EXE"), "-nocutscenes"], cwd=game, env=env)
log(f"started pid {proc.pid}, primary screen {sw}x{sh}")
threading.Thread(target=keep_awake, daemon=True).start()

for raw in open(steps_file):
    line = raw.split("#")[0].strip()
    if not line:
        continue
    op, *a = line.split()
    if op == "front":
        front()
    elif op == "wait":
        time.sleep(float(a[0]))
    elif op == "move":
        move(int(a[0]), int(a[1]))
    elif op == "menu":
        s = min(sw / 854, sh / 480)
        ox, oy = (sw - 854 * s) / 2, (sh - 480 * s) / 2
        move(int(ox + (int(a[0]) + 107 + 0.5) * s), int(oy + (int(a[1]) + 0.5) * s))
    elif op == "click":
        click()
    elif op == "key":
        vk = VK.get(a[0]) or (ord(a[0]) if len(a[0]) == 1 and a[0].isalnum() else None)
        if vk is None:
            log(f"unknown key {a[0]}")
            continue
        key(vk, int(a[1]) if len(a) > 1 else 120)
        log(f"key {a[0]}")
    elif op == "altf4":
        t = time.time()
        key(VK["F4"], 80, alt=True)
        log("Alt+F4")
    elif op == "shot":
        guard()
        ImageGrab.grab(bbox=(0, 0, sw, sh)).save(os.path.join(out, a[0] + ".png"))
        log(f"shot {a[0]}")
    elif op == "expect_exit":
        t = time.time()
        try:
            proc.wait(timeout=float(a[0]))
            log(f"the game ended {time.time() - t:.2f} s later (exit code {proc.returncode})")
        except subprocess.TimeoutExpired:
            log(f"the game was still running {a[0]} s later; ending it")
            proc.terminate()
        sys.exit(0)
    else:
        log(f"unknown step {op}")

proc.wait(timeout=float(env["C2HD_QUIT"]) + 30)
log(f"exit {proc.returncode}")
