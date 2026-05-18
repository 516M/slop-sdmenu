#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

make -s all 2>/dev/null

echo "sdmenu startup benchmark"
echo "========================"
echo ""

# Fresh cache benchmark
rm -f ~/.cache/sdmenu_items
echo "--- first startup (no cache) ---"
SDMENU_BENCH=1 DISPLAY=:99 timeout 3 \
  sh -c '"$0" "$@"' "$DIR/sdmened" 2>&1 | grep -v "^Command" | grep "ms\|us"

echo ""
echo "--- cached startup ---"
SDMENU_BENCH=1 DISPLAY=:99 timeout 3 \
  sh -c '"$0" "$@"' "$DIR/sdmened" 2>&1 | grep -v "^Command" | grep "ms\|us"
