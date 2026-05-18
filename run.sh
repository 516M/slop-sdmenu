#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

if [ -t 0 ]; then
  echo "sdmenu — dmenu clone"
  echo "usage: echo items | $0 [options]"
  echo "  or:  cat list.txt | $0 [options]"
  echo "options: -b -i -l N -m N -p prompt -fn font"
  echo "         -nb color -nf color -sb color -sf color"
  exit 1
fi

make -s sdmenu 2>/dev/null
exec ./sdmenu "$@"
