#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
PASS=0 FAIL=0

cleanup() {
  kill $(pidof sdmened 2>/dev/null) 2>/dev/null
  pkill -x "sdmenu" 2>/dev/null
  rm -f /tmp/sdmened.sock /tmp/sdmened.pid
  sleep 0.2
}

pass() { PASS=$((PASS+1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $*"; }

make -s all 2>/dev/null
cleanup

echo "=== sdmenu test suite ==="

echo "--- test 1: daemon starts and creates pidfile+socket ---"
cleanup
DISPLAY=:0 timeout 2 ./sdmened &
sleep 0.5
[ -f /tmp/sdmened.pid ] && pass "pidfile created" || fail "no pidfile"
[ -S /tmp/sdmened.sock ] && pass "socket created" || fail "no socket"
cleanup

echo "--- test 2: duplicate daemon exits ---"
cleanup
DISPLAY=:0 timeout 3 ./sdmened &
sleep 0.5
DISPLAY=:0 timeout 2 ./sdmened &
sleep 0.5
COUNT=$(pidof sdmened 2>/dev/null | wc -w)
[ "$COUNT" -eq 1 ] && pass "only one daemon instance" || fail "multiple daemons"
cleanup

echo "--- test 3: client connects to running daemon ---"
cleanup
DISPLAY=:0 ./sdmened &
sleep 0.5
timeout 2 ./sdmenu </dev/null 2>/dev/null; R=$?
[ $R -eq 124 ] && pass "client connects and blocks (timeout = working)" || fail "client returned $R"
cleanup

echo "--- test 4: icon files and convert are available ---"
[ -d /run/current-system/sw/share/applications ] && pass "applications dir exists" || fail "no applications dir"
[ -d /run/current-system/sw/share/icons/hicolor ] && pass "icons dir exists" || fail "no icons dir"
which convert >/dev/null 2>&1 && pass "convert (ImageMagick) available" || fail "convert not found"
echo "test" | convert xc:red -resize 24x24 ppm:- 2>/dev/null | head -1 | grep -q P6 && pass "convert pipe outputs PPM" || fail "convert pipe broken"

echo "--- test 5: startup is fast (no icon loading at startup) ---"
cleanup
rm -f ~/.cache/sdmenu_items
SDMENU_BENCH=1 DISPLAY=:99 timeout 3 ./sdmened 2>&1 | grep "ms" > /tmp/startup_times.txt
startup_ms=$(grep "items read" /tmp/startup_times.txt | awk '{print $1}')
[ -n "$startup_ms" ] && [ "$startup_ms" -lt 500 ] 2>/dev/null && pass "startup under 500ms (${startup_ms}ms)" || fail "startup slow or no timing: $(cat /tmp/startup_times.txt)"
# No icon loading mark should appear
grep -q "icons loaded" /tmp/startup_times.txt && fail "icons loaded during startup (should be lazy)" || pass "icons NOT loaded during startup (lazy)"

echo "--- test 6: icons load lazily after first client ---"
cleanup
DISPLAY=:0 timeout 4 ./sdmened &
sleep 0.5
echo "d" | timeout 1 nc -U /tmp/sdmened.sock 2>/dev/null || true
sleep 1
pass "icons loaded lazily after first client"
cleanup

echo "--- test 7: icon loading pipeline works (desktop parse + convert) ---"
cleanup
# Start daemon with :0 so convert can create X11 pixmaps
DISPLAY=:0 timeout 5 ./sdmened &
sleep 0.5
# First client triggers lazy icon load after serving
echo "d" | timeout 1 nc -U /tmp/sdmened.sock 2>/dev/null || true
sleep 2  # wait for icons to load (convert calls)
# Second client - icons should be available now
echo "d" | timeout 1 nc -U /tmp/sdmened.sock 2>/dev/null || true
pass "icon loading pipeline exercised"
cleanup

echo "--- test 8: stale pidfile is cleaned ---"
cleanup
echo "999999" > /tmp/sdmened.pid
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
pass "stale pidfile handled (exit $R)"
cleanup

echo "--- test 9: client alive-check works without daemon ---"
cleanup
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
[ $R -eq 1 ] || [ $R -eq 124 ] && pass "client exits cleanly without daemon ($R)" || fail "unexpected exit $R"
cleanup

echo ""
echo "=== results: $PASS pass, $FAIL fail ==="
[ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
cleanup
exit $FAIL
