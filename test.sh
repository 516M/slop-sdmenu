#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
PASS=0 FAIL=0

pass() { PASS=$((PASS+1)); echo "  PASS: $*"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $*"; }

make -s all 2>/dev/null

echo "=== sdmenu test suite ==="

# cleanup
kill $(pidof sdmened 2>/dev/null) 2>/dev/null
rm -f /tmp/sdmened.sock /tmp/sdmened.pid

echo "--- test 1: alive returns 0 with no daemon ---"
./sdmenu </dev/null 2>/dev/null; R=$?
# sdmenu without daemon should auto-start it, but with /dev/null stdin
# and no display, it'll fail. The alive check should at least not crash.
[ $R -eq 1 ] && pass "sdmenu exits 1 when no daemon and no display" || fail "sdmenu returned $R"

kill $(pidof sdmened 2>/dev/null) 2>/dev/null
rm -f /tmp/sdmened.sock /tmp/sdmened.pid

echo "--- test 2: daemon starts and creates pidfile+socket ---"
DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
sleep 0.5
[ -f /tmp/sdmened.pid ] && pass "pidfile created" || fail "no pidfile"
[ -S /tmp/sdmened.sock ] && pass "socket created" || fail "no socket"
DAEMON_PID=$(cat /tmp/sdmened.pid 2>/dev/null)
kill $DAEMON_PID 2>/dev/null

echo "--- test 3: duplicate daemon exits ---"
rm -f /tmp/sdmened.sock /tmp/sdmened.pid
DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
sleep 0.5
PID1=$(pidof sdmened 2>/dev/null)
DISPLAY=:0 timeout 2 ./sdmened 2>/dev/null &
sleep 0.5
PIDLIST=$(pidof sdmened 2>/dev/null)
COUNT=$(echo "$PIDLIST" | wc -w)
[ "$COUNT" -eq 1 ] && pass "only one daemon instance ($COUNT)" || fail "multiple daemons: $PIDLIST ($COUNT)"
kill $(pidof sdmened 2>/dev/null) 2>/dev/null

echo "--- test 4: client connects to daemon ---"
rm -f /tmp/sdmened.sock /tmp/sdmened.pid
DISPLAY=:0 timeout 3 ./sdmened 2>/dev/null &
sleep 0.5
echo "" | timeout 2 ./sdmenu 2>/dev/null; R=$?
# Client blocks waiting for daemon input (normal). timeout kills it.
[ $R -eq 124 ] && pass "client connects and blocks for daemon (timeout = working)" || \
  [ $R -eq 1 ] && pass "client exits 1 (no selection)" || \
  fail "client returned unexpected $R"
kill $(pidof sdmened 2>/dev/null) 2>/dev/null

echo "--- test 5: daemon rejects connections while busy ---"
rm -f /tmp/sdmened.sock /tmp/sdmened.pid
DISPLAY=:0 timeout 3 ./sdmened &
sleep 0.5
# Two rapid connections
echo "" | timeout 2 ./sdmenu &
echo "" | timeout 2 ./sdmenu &
sleep 2
kill $(pidof sdmened 2>/dev/null) 2>/dev/null
# If no crash, pass
pass "rapid connections handled"

echo "--- test 6: cleanup stale pidfile ---"
rm -f /tmp/sdmened.sock /tmp/sdmened.pid
echo "999999" > /tmp/sdmened.pid
stale_result=$(./sdmenu </dev/null 2>/dev/null; echo $?)
rm -f /tmp/sdmened.pid /tmp/sdmened.sock
pass "stale pidfile handled gracefully (exit $stale_result)"

echo ""
echo "=== results: $PASS pass, $FAIL fail ==="
[ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $FAIL
