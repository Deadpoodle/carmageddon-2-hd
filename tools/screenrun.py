"""On-screen test run: start the game in the visible test mode with a script,
grab the real screen at given times, then wait for the game to quit.

usage: python -E -P tools/screenrun.py <game dir> <out prefix> <quit s> <keys> <t1,t2,...>
The game's own test switches (C2HD_*) come from the environment.
"""
import os
import subprocess
import sys
import time

from PIL import ImageGrab

game, prefix, quit_s, keys, times = sys.argv[1:6]
env = dict(os.environ, C2HD_HEADLESS="2", C2HD_QUIT=quit_s, C2HD_KEYS=keys)
start = time.time()
proc = subprocess.Popen([os.path.join(game, "CARMA2_HW.EXE"), "-nocutscenes"], cwd=game, env=env)
for i, t in enumerate(float(x) for x in times.split(",")):
    delay = start + t - time.time()
    if delay > 0:
        time.sleep(delay)
    ImageGrab.grab().save(f"{prefix}_{i}.png")
    print(f"grab {i} at {time.time() - start:.1f} s", flush=True)
proc.wait(timeout=float(quit_s) + 30)
print("exit", proc.returncode)
