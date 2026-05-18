#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

make -s sdmenu 2>/dev/null

echo "sdmenu benchmark"
echo "================"
echo ""
echo "== startup + match speed =="
SDMENU_BENCH=1 DISPLAY=:99 timeout 3 \
  sh -c '"$0" -l 10' "$DIR/sdmenu" 2>&1 | grep -v "^Command" | grep .
