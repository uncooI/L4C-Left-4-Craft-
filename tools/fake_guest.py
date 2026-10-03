"""A stand-in Skyrim for a second, local Minecraft client, to try SkyCraft multiplayer on one PC.

The guest client (started by tools/run_guest.ps1: -Dskycraft.link=Local\\SkyCraft_guest, --username
Guest) joins the host's world; this script is its "Skyrim". It
  * copies the ground the host's real Skyrim describes (the host's collision messages, read without
    consuming them) to the guest, so the guest stands on the real terrain near the host;
  * starts the guest a few blocks from the host and then follows the host: it turns the guest
    towards the host and walks (sprints when far) whenever the host is more than a few blocks away;
  * keeps the guest's link serviced (heartbeat, render and event rings, overlay).

    python tools/fake_guest.py [seconds] [slot_angle_degrees] [slot_radius]

Several guests: give each its own link (SKYCRAFT_LINK=Local\\SkyCraft_guest2, ...; the client gets
the same via run_guest.ps1 -Link) and its own slot angle, so they spread around the host.
"""
import math
import mmap
import os
import struct
import sys
import time

os.environ.setdefault("SKYCRAFT_LINK", "Local\\SkyCraft_guest")
import fake_skyrim as fs  # noqa: E402  (reads SKYCRAFT_LINK for its mapping name)

KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE, KEY_LCTRL = 26, 4, 22, 7, 44, 224  # SDL scancodes
FOLLOW_FROM, STOP_AT, SPRINT_FROM = 1.5, 0.6, 8.0  # blocks from its spot beside the host


class HostLink:
    """Read-only view of the host's link: its player, and its Skyrim's collision messages."""

    def __init__(self):
        self.m = mmap.mmap(-1, fs.OFF_COL + fs.COL_BYTES, tagname="Local\\SkyCraft_v1")
        self.read_at = self.col_head()  # only what the host's Skyrim sends from now on

    def player(self):
        _, flags, x, y, z = struct.unpack_from("<IIddd", self.m, fs.OFF_MC)
        return flags, (x, y, z)

    def look(self):
        """The host's look direction: Minecraft yaw and pitch (degrees)."""
        return struct.unpack_from("<ff", self.m, fs.OFF_MC + 32)

    def col_head(self):
        return struct.unpack_from("<Q", self.m, fs.OFF_COL)[0]

    def new_collision(self):
        """Collision messages the host's Skyrim wrote since the last call: [(type, payload)]."""
        out = []
        head = self.col_head()
        if head - self.read_at > fs.COL_DATA or head < self.read_at:
            self.read_at = head  # fell too far behind (or the host restarted): start over
            return out
        while self.read_at < head:
            pos = self.read_at % fs.COL_DATA
            typ, n = struct.unpack_from("<II", self.m, fs.OFF_COL + 0x80 + pos)
            if typ == 0:
                self.read_at += fs.COL_DATA - pos
                continue
            start = fs.OFF_COL + 0x80 + pos + 8
            out.append((typ, bytes(self.m[start:start + n])))
            self.read_at += (8 + n + 7) & ~7
        return out


def yaw_towards(frm, to):
    """Minecraft yaw (degrees; 0 faces +Z, 90 faces -X) from one position towards another."""
    return -math.degrees(math.atan2(to[0] - frm[0], to[2] - frm[2]))


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 3600
    angle = math.radians(float(sys.argv[2]) if len(sys.argv) > 2 else 0.0)
    radius = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
    slot = (math.cos(angle) * radius, math.sin(angle) * radius)  # its place beside the host (x, z)
    host = HostLink()
    flags, (hx, hy, hz) = host.player()
    if not flags & 1:
        print("the host's Minecraft isn't in its world yet; start there first")
        return
    spawn = (hx + slot[0], hy + 1.0, hz + slot[1])
    print(f"host at ({hx:.1f}, {hy:.1f}, {hz:.1f}); the guest starts at ({spawn[0]:.1f}, {spawn[1]:.1f}, {spawn[2]:.1f})")

    link = fs.Link()
    epoch = int(time.time()) % 100000 + 2  # a fresh epoch drops whatever ground the guest had
    tseq = epoch + 1
    cleared = False
    held = {KEY_W: False, KEY_A: False, KEY_S: False, KEY_D: False, KEY_LCTRL: False}
    moving = False
    start = time.time()
    last_print = 0.0
    forwarded = 0
    yaw, pitch = -90.0, 10.0

    def key(code, down):
        if held.get(code) != down:
            held[code] = down
            link.push_input(1, code, 1 if down else 0)

    while time.time() - start < seconds:
        link.heartbeat()
        link.acquire_overlay()
        if link.mc_alive() and not cleared:
            link.write_col(1, struct.pack("<I", epoch))
            cleared = True
        # The host's ground, as the guest's own: same messages, the guest's epoch.
        for typ, payload in host.new_collision():
            if typ in (2, 3) and cleared and len(payload) >= 32:
                payload = payload[:24] + struct.pack("<I", epoch) + payload[28:]
                link.write_col(typ, payload)
                forwarded += 1
        mc = link.read_mc()
        _, hpos = host.player()
        in_world = mc["flags"] & 1 and mc["ack"] == tseq
        if in_world and (mc["pos"][1] < hpos[1] - 20.0 or math.hypot(hpos[0] - mc["pos"][0], hpos[2] - mc["pos"][2]) > 64.0):
            # Fell through before its ground arrived, or the host travelled: back beside the host.
            spawn = (hpos[0] + slot[0], hpos[1] + 1.0, hpos[2] + slot[1])
            tseq += 1
            in_world = False
            print(f"guest lost at ({mc['pos'][0]:.1f}, {mc['pos'][1]:.1f}, {mc['pos'][2]:.1f}); teleporting it back beside the host")
        if in_world:
            gpos = mc["pos"]
            target = (hpos[0] + slot[0], hpos[1], hpos[2] + slot[1])
            dist = math.hypot(target[0] - gpos[0], target[2] - gpos[2])
            if dist > FOLLOW_FROM:
                moving = True
            elif dist < STOP_AT:
                moving = False
            # Look where the host looks; walk to its spot with whichever of W/A/S/D point that way
            # (in Minecraft, D steps towards yaw + 90).
            yaw, pitch = host.look()
            diff = (yaw_towards(gpos, target) - yaw + 180.0) % 360.0 - 180.0
            key(KEY_W, moving and abs(diff) < 67.5)
            key(KEY_S, moving and abs(diff) > 112.5)
            key(KEY_D, moving and 22.5 < diff < 157.5)
            key(KEY_A, moving and -157.5 < diff < -22.5)
            key(KEY_LCTRL, held[KEY_W] and abs(diff) < 30.0 and dist > SPRINT_FROM)
        link.write_sky(1, spawn, yaw, pitch, tseq, epoch)
        if time.time() - last_print > 5:
            last_print = time.time()
            g = mc["pos"]
            print(f"t={time.time() - start:6.1f} guest {'in world' if in_world else 'waiting'} at ({g[0]:.1f}, {g[1]:.1f}, {g[2]:.1f}), "
                  f"host at ({hpos[0]:.1f}, {hpos[1]:.1f}, {hpos[2]:.1f}); {forwarded} ground messages copied; walking {moving}")
        time.sleep(0.01)
    for code in list(held):
        key(code, False)


if __name__ == "__main__":
    main()
