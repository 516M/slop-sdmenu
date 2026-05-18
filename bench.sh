#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

make -s sdmenu 2>/dev/null

items="/tmp/sdmenu_bench_items"
dmenu_path > "$items" 2>/dev/null
nitems=$(wc -l < "$items")

echo "sdmenu benchmark — $nitems items"
echo "=============================="
echo ""

echo "== startup + match speed (no X needed) =="
SDMENU_BENCH=1 DISPLAY=:99 timeout 2 \
  sh -c 'cat '"$items"' | '"$DIR"'/sdmenu -l 10' 2>&1 | grep -v "^Command" | grep .
