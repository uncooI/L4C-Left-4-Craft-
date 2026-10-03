"""Read live SkyState/McState and show the collision voxels under the player (read-only)."""
import mmap, struct
NAME = r"Local\SkyCraft_v1"
OFF_COL = 0x20000
COL_BYTES = 32 << 20
DATA = COL_BYTES - 0x80
m = mmap.mmap(-1, 0x20000 + COL_BYTES, tagname=NAME, access=mmap.ACCESS_READ)
sky = struct.unpack_from("<IIIIdddffIIIf", m, 0x100)
mc = struct.unpack_from("<IIdddffffIIQfffIddd", m, 0x200)
print("sky flags=%#x world=%#x epoch=%d pos=(%.2f %.2f %.2f) yaw=%.1f pitch=%.1f tpseq=%d" % (sky[1], sky[2], sky[3], sky[4], sky[5], sky[6], sky[7], sky[8], sky[9]))
print("mc  flags=%#x pos=(%.2f %.2f %.2f) yaw=%.1f ack=%d" % (mc[1], mc[2], mc[3], mc[4], mc[5], mc[9]))
head, = struct.unpack_from("<Q", m, OFF_COL)
blocks = {}
pos = 0 if head <= DATA else head - DATA
epoch_now = sky[3]
while pos < head:
    p = pos % DATA
    typ, n = struct.unpack_from("<II", m, OFF_COL + 0x80 + p)
    if typ == 0:
        pos += DATA - p; continue
    body = OFF_COL + 0x80 + p + 8
    if typ == 2:
        mnx, mny, mnz, mxx, mxy, mxz, ep, cnt = struct.unpack_from("<iiiiiiII", m, body)
        if ep == epoch_now:
            for x in range(mnx, mxx + 1):
                for y in range(mny, mxy + 1):
                    for z in range(mnz, mxz + 1):
                        blocks.pop((x, y, z), None)
            for i in range(cnt):
                b = body + 32 + i * 80
                x, y, z, _ = struct.unpack_from("<iiiI", m, b)
                blocks[(x, y, z)] = struct.unpack_from("<8Q", m, b + 16)
    pos += (8 + n + 7) & ~7
px, py, pz = sky[4], sky[5], sky[6]
bx, bz = int(px // 1), int(pz // 1)
print("column under player (block y: filled sub-voxels /512):")
for y in range(int(py) + 3, int(py) - 6, -1):
    row = []
    for dz in (-1, 0, 1):
        for dx in (-1, 0, 1):
            bits = blocks.get((bx + dx, y, bz + dz))
            row.append("%3d" % (sum(bin(v).count("1") for v in bits) if bits else 0))
    print("  y=%4d  %s" % (y, " ".join(row)))
