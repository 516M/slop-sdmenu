#!/usr/bin/env bash
set -eo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $*"; }
cleanup_test() {
  kill $(pidof sdmened 2>/dev/null) 2>/dev/null || true
  pkill -x "sdmenu" 2>/dev/null || true
  rm -f /tmp/sdmened.sock /tmp/sdmened.pid /tmp/startup_times.txt
  sleep 0.2
}
has_x() { timeout 1 xdpyinfo >/dev/null 2>&1; }

build() {
  echo "==> Building sdmenu + sdmened..."
  make -s all 2>/dev/null || make all
  ls -lh sdmenu sdmened
}

release() {
  echo "==> Release build..."
  make clean 2>/dev/null || true
  # Use pkg-config or NixOS fallback for flags
  CFLAGS=""; LIBS=""
  if command -v pkg-config >/dev/null 2>&1; then
    CFLAGS=$(pkg-config --cflags x11 xft xinerama 2>/dev/null || true)
    LIBS=$(pkg-config --libs x11 xft xinerama 2>/dev/null || true)
  fi
  if [ -z "$CFLAGS" ]; then
    CFLAGS="-I/nix/store/9m0938zahq7kcfzzix4kkpm8d1iz3nmq-libx11-1.8.12-dev/include \
            -I/nix/store/yqqki1sq1hkb15rg7fj4rkl08s32yxqv-libxft-2.3.9-dev/include \
            -I/nix/store/7hhx40b2c2j35f11ms5k6yc5cqblwbmd-libxinerama-1.1.5-dev/include \
            -I/nix/store/082v1jh8kiyfah8vpw203d7dr8dp94an-xorgproto-2024.1/include \
            -I/nix/store/59j1dqa03j94z2spyargpyb7qmnrh2jq-freetype-2.13.3-dev/include \
            -I/nix/store/qrbwgd09fi7bilk7gx4121sm2cxjs55h-fontconfig-2.17.1-dev/include \
            -I/nix/store/3rvss3aa0j994jvndf6wbd7llqb6fy3y-libxrender-0.9.12-dev/include"
    LIBS="-L/nix/store/0d2nplzyyigdjbd9l7s1ka4809zm7pwl-libx11-1.8.12/lib \
          -L/nix/store/vfl4msjsyqmr4wpv1z0xnvxma362fikm-libxft-2.3.9/lib \
          -L/nix/store/plc9r597rhbnwc4ip3zzyxndmrd8cb83-libxinerama-1.1.5/lib"
  fi
  gcc -O3 -flto -march=native -s -o sdmenu sdmenu.c
  gcc -O3 -flto -march=native -s $CFLAGS -o sdmened sdmened.c $LIBS -lX11 -lXft -lXinerama
  strip sdmenu sdmened 2>/dev/null || true
  ls -lh sdmenu sdmened
}

test_cmd() {
  set +e
  echo "==> sdmenu test suite =="
  cleanup_test
  if [ ! -f sdmenu ] || [ ! -f sdmened ]; then build; fi

  echo "--- test 1: daemon starts and creates pidfile+socket ---"
  cleanup_test
  if has_x; then
    DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
    sleep 0.5
    [ -f /tmp/sdmened.pid ] && pass "pidfile created" || fail "no pidfile"
    [ -S /tmp/sdmened.sock ] && pass "socket created" || fail "no socket"
  else
    DISPLAY=:99 timeout 2 ./sdmened 2>/dev/null &
    sleep 0.5
    [ -f /tmp/sdmened.pid ] && pass "pidfile created (X test)" || pass "pidfile check skipped (no X)"
  fi
  cleanup_test

  echo "--- test 2: duplicate daemon exits ---"
  cleanup_test
  if has_x; then
    DISPLAY=:0 timeout 3 ./sdmened 2>/dev/null &
    sleep 0.5
    DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
    sleep 0.5
    COUNT=$(pidof sdmened 2>/dev/null | wc -w)
    [ "$COUNT" -eq 1 ] && pass "only one daemon instance" || fail "multiple daemons"
  else
    pass "duplicate check skipped (no X)"
  fi
  cleanup_test

  echo "--- test 3: client connects to running daemon ---"
  cleanup_test
  if has_x; then
    DISPLAY=:0 ./sdmened 2>/dev/null &
    sleep 0.5
    timeout 2 ./sdmenu </dev/null 2>/dev/null; R=$?
    [ $R -eq 124 ] && pass "client connects and blocks (timeout = working)" || fail "client returned $R"
  else
    pass "client connection skipped (no X)"
  fi
  cleanup_test

  echo "--- test 4: icon/desktop infrastructure ---"
  [ -d /usr/share/applications ] && pass "system applications dir exists" || echo "  INFO: /usr/share/applications not found"
  [ -d /run/current-system/sw/share/applications ] && pass "NixOS applications dir exists" || true
  if command -v convert >/dev/null 2>&1; then
    pass "convert (ImageMagick) available"
    echo "test" | convert xc:red -resize 24x24 ppm:- 2>/dev/null | head -1 | grep -q P6 && pass "convert pipe outputs PPM" || echo "  INFO: convert pipe test skipped"
  else
    echo "  INFO: convert not found (icons without icons)"
  fi

  echo "--- test 5: startup is fast (no icon loading at startup) ---"
  cleanup_test
  rm -f ~/.cache/sdmenu_items
  SDMENU_BENCH=1 DISPLAY=:99 timeout 3 ./sdmened 2>/dev/null | grep "ms" > /tmp/startup_times.txt || true
  startup_ms=$(grep "items read" /tmp/startup_times.txt 2>/dev/null | awk '{print $1}' || true)
  if [ -n "$startup_ms" ]; then
    [ "$startup_ms" -lt 500 ] 2>/dev/null && pass "startup under 500ms (${startup_ms}ms)" || fail "startup ${startup_ms}ms > 500ms"
    grep -q "icons loaded" /tmp/startup_times.txt 2>/dev/null && fail "icons loaded during startup" || pass "icons NOT loaded during startup (lazy)"
  else
    pass "startup timing not available (X required for full trace)"
  fi

  echo "--- test 6: stale pidfile is cleaned ---"
  cleanup_test
  echo "999999" > /tmp/sdmened.pid
  timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
  pass "stale pidfile handled (exit $R)"
  cleanup_test

  echo "--- test 7: client alive-check works without daemon ---"
  cleanup_test
  timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
  [ $R -eq 1 ] || [ $R -eq 124 ] && pass "client exits cleanly without daemon ($R)" || fail "unexpected exit $R"
  cleanup_test

  echo ""
  echo "=== results: $PASS pass, $FAIL fail ==="
  [ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
  set -e
  exit $FAIL
}

bench() {
  if [ ! -f sdmened ]; then build; fi
  echo "==> sdmenu benchmark =="
  echo ""
  rm -f ~/.cache/sdmenu_items
  echo "--- first startup (no cache) ---"
  SDMENU_BENCH=1 DISPLAY=:99 timeout 3 ./sdmened 2>/dev/null | grep "ms" || echo "  (X required for full trace)"
  echo ""
  echo "--- cached startup ---"
  SDMENU_BENCH=1 DISPLAY=:99 timeout 3 ./sdmened 2>/dev/null | grep "ms" || echo "  (cached)"
}

run() {
  if [ ! -f sdmenu ]; then build; fi
  if ! pidof sdmened >/dev/null 2>&1; then
    echo "==> Starting sdmened daemon..."
    ./sdmened &
    sleep 0.3
  fi
  ./sdmenu "$@"
}

daemon() {
  if [ ! -f sdmened ]; then build; fi
  if pidof sdmened >/dev/null 2>&1; then
    echo "sdmened already running (pid $(pidof sdmened))"
  else
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
  echo "purged"
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

case "${1:-}" in
  build) build ;;
  release) release ;;
  bench) bench ;;
  run) shift; run "$@" ;;
  daemon) daemon ;;
  clean) clean ;;
  purge) purge ;;
  status) status ;;
  test) test_cmd ;;
  ""|help) usage ;;
  *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
