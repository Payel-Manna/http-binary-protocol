#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

SERVER="$ROOT/build/serve"
CLIENT="$ROOT/build/bincurl"

PORT=19000
LOG="/tmp/http_binary_server.log"

echo "Starting server..."

stdbuf -oL -eL "$SERVER" "$ROOT/www" "$PORT" >"$LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
}

trap cleanup EXIT

sleep 1
# Wait until the server has actually started listening.
for i in {1..20}; do
    if grep -q "listening on port $PORT" "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q "listening on port $PORT" "$LOG"; then
    echo "FAIL: server did not start"
    cat "$LOG"
    exit 1
fi

echo
echo "================================"
echo "TEST 1: Existing file"
echo "================================"

"$CLIENT" \
    "127.0.0.1:$PORT/index.html" \
    >/tmp/test_output.txt

grep -q "HTTP status: 200" \
    /tmp/test_output.txt

echo "PASS: existing file returned 200"


echo
echo "================================"
echo "TEST 2: Missing file"
echo "================================"

set +e

"$CLIENT" \
    "127.0.0.1:$PORT/does-not-exist.txt" \
    >/tmp/test_404.txt

RESULT=$?

set -e

grep -q "HTTP status: 404" \
    /tmp/test_404.txt

grep -q "not found" \
    /tmp/test_404.txt

echo "PASS: missing file returned 404"

echo
echo "================================"
echo "TEST 3: Persistent connection"
echo "================================"

BEFORE=$(
    grep -c "accepted TCP connection" \
        "$LOG" || true
)

"$CLIENT" \
    "127.0.0.1:$PORT/index.html" \
    "127.0.0.1:$PORT/hello.txt" \
    >/tmp/test_persistent.txt

# Give the server a moment to flush its connection log.
for i in {1..20}; do
    AFTER=$(
        grep -c "accepted TCP connection" \
            "$LOG" || true
    )

    if [ "$AFTER" -gt "$BEFORE" ]; then
        break
    fi

    sleep 0.1
done

NEW_CONNECTIONS=$((AFTER - BEFORE))

if [ "$NEW_CONNECTIONS" -ne 1 ]; then
    echo "FAIL: expected exactly 1 new TCP connection"
    echo "New connections: $NEW_CONNECTIONS"
    echo
    echo "Server log:"
    cat "$LOG"
    exit 1
fi

COUNT=$(
    grep -c "HTTP status: 200" \
        /tmp/test_persistent.txt
)

if [ "$COUNT" -ne 2 ]; then
    echo "FAIL: expected two HTTP 200 responses"
    cat /tmp/test_persistent.txt
    exit 1
fi

echo "PASS: two requests used one TCP connection"
echo
echo "================================"
echo "ALL INTEGRATION TESTS PASSED"
echo "================================"