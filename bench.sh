#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

make -s all 2>/dev/null

echo "sdmenu benchmark — $(dirname "$0")"
echo "=============================="
echo ""

if [ -f ~/.cache/sdmenu_items ]; then
  nitems=$(wc -l < ~/.cache/sdmenu_items)
  echo "== daemon startup (cached, no X) =="
  SDMENU_BENCH=1 DISPLAY=:99 timeout 3 \
    sh -c '"$0" "$@"' "$DIR/sdmened" 2>&1 | grep -v "^Command" | grep .
else
  echo "(run sdmenu first to generate cache)"
fi
