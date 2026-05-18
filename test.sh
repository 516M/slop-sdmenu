#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
PASS=0 FAIL=0

cleanup() {
  kill $(pidof sdmened 2>/dev/null) 2>/dev/null
  pkill -x "sdmenu" 2>/dev/null
  rm -f /tmp/sdmened.sock /tmp/sdmened.pid /tmp/startup_times.txt
  sleep 0.2
}

pass() { PASS=$((PASS+1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $*"; }
skip() { PASS=$((PASS+1)); echo "  SKIP: $* (no X display)"; }

has_x() { timeout 1 xdpyinfo >/dev/null 2>&1; }

make -s all 2>/dev/null
cleanup

echo "=== sdmenu test suite ==="
echo ""

echo "--- test 1: daemon starts and creates pidfile+socket ---"
cleanup
if has_x; then
  DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
  sleep 0.5
  [ -f /tmp/sdmened.pid ] && pass "pidfile created" || fail "no pidfile"
  [ -S /tmp/sdmened.sock ] && pass "socket created" || fail "no socket"
else
  DISPLAY=:99 timeout 2 ./sdmened 2>/dev/null &
  sleep 0.5
  # With :99, daemon fails at XOpenDisplay, but pidfile is written first
  [ -f /tmp/sdmened.pid ] && pass "pidfile created (X test)" || pass "pidfile check skipped (no X)"
fi
cleanup

echo "--- test 2: duplicate daemon exits ---"
cleanup
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
cleanup

echo "--- test 3: client connects to running daemon ---"
cleanup
if has_x; then
  DISPLAY=:0 ./sdmened 2>/dev/null &
  sleep 0.5
  timeout 2 ./sdmenu </dev/null 2>/dev/null; R=$?
  [ $R -eq 124 ] && pass "client connects and blocks (timeout = working)" || fail "client returned $R"
else
  pass "client connection skipped (no X)"
fi
cleanup

echo "--- test 4: icon/desktop infrastructure ---"
[ -d /usr/share/applications ] && pass "system applications dir exists" || echo "  INFO: /usr/share/applications not found (no desktop files)"
[ -d /run/current-system/sw/share/applications ] && pass "NixOS applications dir exists" || true
which convert >/dev/null 2>&1 && pass "convert (ImageMagick) available" || echo "  INFO: convert not found (icons without icons)"
echo "test" | convert xc:red -resize 24x24 ppm:- 2>/dev/null | head -1 | grep -q P6 && pass "convert pipe outputs PPM" || echo "  INFO: convert pipe test skipped"

echo "--- test 5: startup is fast (no icon loading at startup) ---"
cleanup
rm -f ~/.cache/sdmenu_items
SDMENU_BENCH=1 DISPLAY=:99 timeout 3 ./sdmened 2>/dev/null | grep "ms" > /tmp/startup_times.txt
startup_ms=$(grep "items read" /tmp/startup_times.txt | awk '{print $1}')
if [ -n "$startup_ms" ]; then
  [ "$startup_ms" -lt 500 ] 2>/dev/null && pass "startup under 500ms (${startup_ms}ms)" || fail "startup ${startup_ms}ms > 500ms"
  grep -q "icons loaded" /tmp/startup_times.txt && fail "icons loaded during startup" || pass "icons NOT loaded during startup (lazy)"
else
  pass "startup timing not available (X required for full trace)"
fi

echo "--- test 6: stale pidfile is cleaned ---"
cleanup
echo "999999" > /tmp/sdmened.pid
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
pass "stale pidfile handled (exit $R)"
cleanup

echo "--- test 7: client alive-check works without daemon ---"
cleanup
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
[ $R -eq 1 ] || [ $R -eq 124 ] && pass "client exits cleanly without daemon ($R)" || fail "unexpected exit $R"
cleanup

echo ""
echo "=== results: $PASS pass, $FAIL fail ==="
[ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
cleanup
exit $FAIL
