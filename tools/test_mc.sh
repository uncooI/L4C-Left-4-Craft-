#!/usr/bin/env bash
# Runs the Minecraft dev client against tools/fake_skyrim.py and prints what happened.
# Usage: tools/test_mc.sh <out-dir> [seconds]
set -u
OUT=${1:?out dir}; SECS=${2:-120}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
python3 -u "$ROOT/tools/fake_skyrim.py" "$SECS" "$OUT/overlay.png" > "$OUT/fake.log" 2>&1 &
FAKE=$!
(cd "$ROOT/fabric" && ./gradlew runClient --no-configuration-cache > "$OUT/mc.log" 2>&1) &
for _ in $(seq 1 $((SECS / 2))); do
  sleep 2
  grep -q -E "Traceback" "$OUT/fake.log" && break
  grep -q -E "Crash report|BUILD FAILED|Exception in thread" "$OUT/mc.log" && break
  if grep -q "saved overlay" "$OUT/fake.log"; then sleep 4; break; fi
done
powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='java.exe'\" | Where-Object { \$_.CommandLine -match 'fabric.dli' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1
kill $FAKE 2>/dev/null
echo "=== fake_skyrim"; grep -v "^t=" "$OUT/fake.log"; grep "^t=" "$OUT/fake.log" | tail -12
echo "=== minecraft"; grep -i -E "skycraft|mixin apply|InvalidInjection|Crash|Exception" "$ROOT/fabric/run/logs/latest.log" | grep -v "^\s*at " | tail -25
