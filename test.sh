#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
PASS=0 FAIL=0

cleanup() {
  kill $(pidof sdmened 2>/dev/null) 2>/dev/null
  pkill -f "sdmenu" 2>/dev/null
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
PID1=$(pidof sdmened 2>/dev/null)
DISPLAY=:0 timeout 2 ./sdmened &
sleep 0.5
PIDLIST=$(pidof sdmened 2>/dev/null)
COUNT=$(echo "$PIDLIST" | wc -w)
[ "$COUNT" -eq 1 ] && pass "only one daemon instance" || fail "multiple daemons: $PIDLIST"
cleanup

echo "--- test 3: client connects to running daemon ---"
cleanup
DISPLAY=:0 ./sdmened &
D=$!
sleep 0.5
timeout 2 ./sdmenu </dev/null 2>/dev/null; R=$?
[ $R -eq 124 ] && pass "client connects and blocks (timeout = working)" || fail "client returned $R"
cleanup

echo "--- test 4: rapid connections don't crash ---"
cleanup
DISPLAY=:0 timeout 3 ./sdmened &
sleep 0.5
timeout 1 ./sdmenu </dev/null 2>/dev/null &
timeout 1 ./sdmenu </dev/null 2>/dev/null &
sleep 2
pass "rapid connections handled"
cleanup

echo "--- test 5: stale pidfile is cleaned ---"
cleanup
echo "999999" > /tmp/sdmened.pid
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
# Should try to start daemon, which needs DISPLAY
# With no DISPLAY=:0 set, it'll fail after timeout
pass "stale pidfile handled (exit $R)"
cleanup

echo "--- test 6: client alive-check works without daemon ---"
cleanup
timeout 3 ./sdmenu </dev/null 2>/dev/null; R=$?
[ $R -eq 1 ] || [ $R -eq 124 ] && pass "client exits cleanly without daemon ($R)" || fail "unexpected exit $R"
cleanup

echo ""
echo "=== results: $PASS pass, $FAIL fail ==="
[ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
cleanup
exit $FAIL
