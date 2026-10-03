"""Records how the player's motion reaches Skyrim, to diagnose camera and body jitter.

Read-only: it samples the link's McState (written by Minecraft every frame and every physics tick)
as fast as it can and writes two CSV files:
  ticks.csv   one row per Minecraft physics tick: when it was stamped, when it became visible,
              previous and latest feet, eye height
  frames.csv  one row per Minecraft frame: camera mode and zoom distance, feet, eye, look, bob

    python tools/trace_motion.py [seconds] [out_dir]

Then walk (uphill, in third person, ...) while it runs.
"""
import ctypes
import csv
import mmap
import os
import struct
import sys
import time

OFF_MC = 0x200
MC_BYTES = 0xC8

_k32 = ctypes.windll.kernel32


def qpc():
    v = ctypes.c_int64()
    _k32.QueryPerformanceCounter(ctypes.byref(v))
    return v.value


def qpc_freq():
    v = ctypes.c_int64()
    _k32.QueryPerformanceFrequency(ctypes.byref(v))
    return v.value


def read_mc(m):
    b = m[OFF_MC:OFF_MC + MC_BYTES]
    seq, flags, x, y, z, yaw, pitch, eye_h, _sens, ack, _gui, frame, fov, bob_phase, bob_amt = struct.unpack_from("<IIdddffffIIQfff", b, 0)
    eye = struct.unpack_from("<ddd", b, 0x50)
    tick_qpc, = struct.unpack_from("<q", b, 0x68)
    prev = struct.unpack_from("<ddd", b, 0x70)
    cur = struct.unpack_from("<ddd", b, 0x88)
    eye_o, eye_t, walk_o, walk, bob_o, bob, tick_ms = struct.unpack_from("<fffffff", b, 0xA0)
    cam_mode, cam_dist = struct.unpack_from("<If", b, 0xC0)
    return dict(seq=seq, flags=flags, x=x, y=y, z=z, yaw=yaw, pitch=pitch, frame=frame, bob_phase=bob_phase, bob_amt=bob_amt,
                eye=eye, tick_qpc=tick_qpc, prev=prev, cur=cur, eye_o=eye_o, eye_t=eye_t, tick_ms=tick_ms,
                cam_mode=cam_mode, cam_dist=cam_dist)


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    out = sys.argv[2] if len(sys.argv) > 2 else "."
    os.makedirs(out, exist_ok=True)
    name = os.environ.get("SKYCRAFT_LINK", "Local\\SkyCraft_v1")
    m = mmap.mmap(-1, 0x1000, tagname=name)
    freq = qpc_freq()
    t0 = qpc()
    ticks, frames = [], []
    last_tick = last_frame = None
    end = time.perf_counter() + seconds
    while time.perf_counter() < end:
        s = read_mc(m)
        now = qpc()
        if s["tick_qpc"] != last_tick:
            last_tick = s["tick_qpc"]
            ticks.append([(s["tick_qpc"] - t0) * 1000.0 / freq, (now - t0) * 1000.0 / freq, *s["prev"], *s["cur"], s["eye_o"], s["eye_t"], s["tick_ms"], s["flags"]])
        if s["frame"] != last_frame:
            last_frame = s["frame"]
            frames.append([(now - t0) * 1000.0 / freq, s["frame"], s["cam_mode"], s["cam_dist"], s["x"], s["y"], s["z"], *s["eye"], s["yaw"], s["pitch"], s["bob_phase"], s["bob_amt"], s["flags"]])
    with open(os.path.join(out, "ticks.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["tick_ms", "seen_ms", "px", "py", "pz", "cx", "cy", "cz", "eye_o", "eye_t", "ms_per_tick", "flags"])
        w.writerows(ticks)
    with open(os.path.join(out, "frames.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["seen_ms", "frame", "cam_mode", "cam_dist", "x", "y", "z", "ex", "ey", "ez", "yaw", "pitch", "bob_phase", "bob_amt", "flags"])
        w.writerows(frames)
    print(f"{len(ticks)} ticks, {len(frames)} Minecraft frames in {seconds:.0f} s -> {out}")


if __name__ == "__main__":
    main()
