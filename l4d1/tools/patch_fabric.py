#!/usr/bin/env python3
"""Apply the verified 260 -> 60 FPS immediate to upstream SkyCraft 0.1.2.
This preserves bytecode lengths, branch offsets, stack maps and wire protocol.
Use with the original release JAR, never repeatedly with an already patched JAR.
"""
import argparse, hashlib, zipfile
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('upstream', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
with zipfile.ZipFile(args.upstream) as source, zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as target:
    for entry in source.infolist():
        data = source.read(entry.filename)
        if entry.filename == 'dev/skycraft/client/SkyClient.class':
            needle = bytes.fromhex('110104')  # sipush 260, verified by javap at applyLinkedOptions+40
            if data.count(needle) != 1:
                raise SystemExit('Unexpected class: refusing a patch without a unique verified immediate.')
            data = data.replace(needle, bytes.fromhex('11003c'))  # sipush 60
        target.writestr(entry, data)
print(hashlib.sha256(args.output.read_bytes()).hexdigest(), args.output)
