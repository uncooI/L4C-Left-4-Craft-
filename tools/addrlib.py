import os
# Address Library (format 5, Skyrim 1.7.99+) lookup: python tools/addrlib.py <id> [...] prints each ID's address, or None
# if this game build lacks it. Check every CommonLib RELOCATION_ID / REL::ID before relying on it.
import struct, sys
DB = os.environ.get("SKYCRAFT_ADDRLIB") or os.path.expandvars(
    "%LOCALAPPDATA%/ModOrganizer/Skyrim Special Edition/mods/Address Library All in One/SKSE/Plugins/versionlib-1-7-104-0.bin")
BASE = 0x140000000
_d = open(DB, 'rb').read()
assert struct.unpack_from('<i', _d, 0)[0] == 5
_count = struct.unpack_from('<i', _d, 92)[0]
def rva(i):
    return struct.unpack_from('<I', _d, 96 + 4 * i)[0] if 0 <= i < _count else None
def addr(i):
    r = rva(i); return BASE + r if r else None
_rev = None
def id_of(a):
    global _rev
    if _rev is None:
        _rev = {}
        for i in range(_count):
            r = struct.unpack_from('<I', _d, 96 + 4 * i)[0]
            if r: _rev.setdefault(r, i)
    return _rev.get(a - BASE)
if __name__ == '__main__':
    print('count', _count, 'version', struct.unpack_from('<4I', _d, 4))
    for a in sys.argv[1:]:
        i = int(a); print(i, hex(addr(i)) if addr(i) else None)
