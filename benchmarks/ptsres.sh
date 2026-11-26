#!/bin/bash
# Prints each PTS result as "test | option | value unit".
L=/var/tmp/bench-out/pts.log
awk '
  /^    (pts|system)\/[a-z0-9-]+-[0-9.]+/ { split($1, a, "-[0-9]"); test=$1; sub(/-[0-9.]+$/, "", test); opt="" }
  /^    [^ ].*:$/ && !/^    (pts|system)\// { opt=$0; gsub(/^ +|:$/, "", opt) }
  /^    Average: / { v=$0; sub(/.*Average: /, "", v); printf "%s|%s|%s\n", test, opt, v }
' "$L"
