#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

RELEASE_FLAGS="-O3 -flto -march=native -s"

build() {
  echo "==> Building sdmenu + sdmened..."
  make -s all 2>/dev/null || make all
  ls -lh sdmenu sdmened
}

release() {
  echo "==> Release build ($RELEASE_FLAGS)..."
  # Force clean build with release flags
  make clean 2>/dev/null || true
  # Manually compile with exact flags for reproducibility
  gcc $RELEASE_FLAGS -o sdmenu sdmenu.c
  gcc $RELEASE_FLAGS $(pkg-config --cflags x11 xft xinerama 2>/dev/null || \
    echo "-I/nix/store/9m0938zahq7kcfzzix4kkpm8d1iz3nmq-libx11-1.8.12-dev/include \
          -I/nix/store/yqqki1sq1hkb15rg7fj4rkl08s32yxqv-libxft-2.3.9-dev/include \
          -I/nix/store/7hhx40b2c2j35f11ms5k6yc5cqblwbmd-libxinerama-1.1.5-dev/include \
          -I/nix/store/082v1jh8kiyfah8vpw203d7dr8dp94an-xorgproto-2024.1/include \
          -I/nix/store/59j1dqa03j94z2spyargpyb7qmnrh2jq-freetype-2.13.3-dev/include \
          -I/nix/store/qrbwgd09fi7bilk7gx4121sm2cxjs55h-fontconfig-2.17.1-dev/include \
          -I/nix/store/3rvss3aa0j994jvndf6wbd7llqb6fy3y-libxrender-0.9.12-dev/include") \
    -o sdmened sdmened.c \
    $(pkg-config --libs x11 xft xinerama 2>/dev/null || \
      echo "-L/nix/store/0d2nplzyyigdjbd9l7s1ka4809zm7pwl-libx11-1.8.12/lib \
            -L/nix/store/vfl4msjsyqmr4wpv1z0xnvxma362fikm-libxft-2.3.9/lib \
            -L/nix/store/plc9r597rhbnwc4ip3zzyxndmrd8cb83-libxinerama-1.1.5/lib") \
    -lX11 -lXft -lXinerama
  ls -lh sdmenu sdmened
  echo "==> Stripping (if not already stripped)..."
  strip sdmenu sdmened 2>/dev/null || true
  ls -lh sdmenu sdmened
}

test() {
  echo "==> Running tests..."
  if [ ! -f sdmenu ] || [ ! -f sdmened ]; then
    build
  fi
  bash test.sh
}

bench() {
  echo "==> Benchmark..."
  if [ ! -f sdmened ]; then
    build
  fi
  bash bench.sh
}

run() {
  if [ ! -f sdmenu ]; then
    build
  fi
  # Auto-start daemon if not running
  if ! pidof sdmened >/dev/null 2>&1; then
    echo "==> Starting sdmened daemon..."
    ./sdmened &
    sleep 0.3
  fi
  echo "==> sdmenu $*"
  ./sdmenu "$@"
}

daemon() {
  if [ ! -f sdmened ]; then
    build
  fi
  if pidof sdmened >/dev/null 2>&1; then
    echo "sdmened already running (pid $(pidof sdmened))"
  else
    echo "==> Starting sdmened..."
    ./sdmened &
    sleep 0.3
    if pidof sdmened >/dev/null 2>&1; then
      echo "sdmened running (pid $(pidof sdmened))"
    else
      echo "ERROR: sdmened failed to start"
      exit 1
    fi
  fi
}

clean() {
  make clean 2>/dev/null || true
  rm -f sdmenu sdmened
  echo "cleaned"
}

purge() {
  clean
  rm -rf ~/.cache/sdmenu_items ~/.cache/sdmenu_icons
  kill $(pidof sdmened 2>/dev/null) 2>/dev/null || true
  rm -f /tmp/sdmened.sock /tmp/sdmened.pid
  echo "purged (binaries + caches + daemon)"
}

status() {
  echo "=== sdmenu status ==="
  echo -n "sdmenu binary:    "; [ -f sdmenu ] && echo "yes ($(stat -c%s sdmenu 2>/dev/null || stat -f%z sdmenu 2>/dev/null) bytes)" || echo "NOT BUILT"
  echo -n "sdmened binary:    "; [ -f sdmened ] && echo "yes ($(stat -c%s sdmened 2>/dev/null || stat -f%z sdmened 2>/dev/null) bytes)" || echo "NOT BUILT"
  echo -n "daemon running:    "; pidof sdmened >/dev/null 2>&1 && echo "yes (pid $(pidof sdmened))" || echo "no"
  echo -n "socket:            "; [ -S /tmp/sdmened.sock ] && echo "yes" || echo "no"
  echo -n "pidfile:           "; [ -f /tmp/sdmened.pid ] && echo "yes" || echo "no"
  echo -n "item cache:        "; [ -f ~/.cache/sdmenu_items ] && echo "yes ($(wc -l < ~/.cache/sdmenu_items 2>/dev/null) items)" || echo "no"
  echo -n "icon cache:        "; ls ~/.cache/sdmenu_icons/ 2>/dev/null | wc -l | xargs echo "icons"
}

usage() {
  echo "sdmenu — build helper"
  echo ""
  echo "Usage: ./build.sh <command> [args]"
  echo ""
  echo "Commands:"
  echo "  build       Compile sdmenu + sdmened"
  echo "  release     Optimized build with -O3 -flto -march=native -s"
  echo "  test        Run test suite"
  echo "  bench       Run benchmarks"
  echo "  run [opts]  Run sdmenu (auto-starts daemon)"
  echo "  daemon      Start sdmened in background"
  echo "  clean       Remove binaries"
  echo "  purge       Remove binaries + caches + kill daemon"
  echo "  status      Show state of all components"
  echo "  help        Show this message"
}

case "${1:-help}" in
  build|release|test|bench|run|daemon|clean|purge|status|help) "$1" "${@:2}" ;;
  *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
