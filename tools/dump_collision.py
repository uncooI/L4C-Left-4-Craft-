"""Decode the collision ring in the live SkyCraft shared memory (read-only) and summarize it."""
import mmap, struct, sys
NAME = "Local\SkyCraft_v1"
OFF_COL = 0x20000
COL_BYTES = 32 << 20
DATA = COL_BYTES - 0x80
m = mmap.mmap(-1, 0x20000 + COL_BYTES, tagname=NAME, access=mmap.ACCESS_READ)
head, = struct.unpack_from("<Q", m, OFF_COL)
tail, = struct.unpack_from("<Q", m, OFF_COL + 0x40)
print(f"head={head} tail={tail}")
pos = 0 if head <= DATA else head - DATA
regions = []
while pos < head:
    p = pos % DATA
    typ, n = struct.unpack_from("<II", m, OFF_COL + 0x80 + p)
    if typ == 0:
        pos += DATA - p
        continue
    body = OFF_COL + 0x80 + p + 8
    if typ == 1:
        print("CLEAR epoch", struct.unpack_from("<I", m, body)[0])
    elif typ == 2:
        mnx, mny, mnz, mxx, mxy, mxz, ep, cnt = struct.unpack_from("<iiiiiiII", m, body)
        bits = 0
        for i in range(cnt):
            b = body + 32 + i * 80
            bits += sum(bin(v).count("1") for v in struct.unpack_from("<8Q", m, b + 16))
        regions.append(((mnx // 8, mny // 8, mnz // 8), ep, cnt, bits))
    pos += (8 + n + 7) & ~7
print(f"{len(regions)} regions; non-empty: {sum(1 for r in regions if r[2])}")
if len(sys.argv) > 1:
    px, py, pz = map(float, sys.argv[1:4])
    key = (int(px // 8), int(py // 8), int(pz // 8))
    print("player region", key)
for r in regions[-40:]:
    print(r)
