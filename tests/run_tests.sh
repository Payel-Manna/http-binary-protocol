#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make >/dev/null

PORT=19001
TMP="$(mktemp -d)"
PID=""
cleanup() {
  if [[ -n "$PID" ]]; then kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; fi
  rm -rf "$TMP"
}
trap cleanup EXIT

cp -r www "$TMP/www"
./build/serve "$TMP/www" "$PORT" >"$TMP/server.log" 2>&1 &
PID=$!
sleep 0.2

./build/bincurl "127.0.0.1:$PORT/index.html" >"$TMP/index.out"
grep -q "Hello from Binary HTTP" "$TMP/index.out"

echo "PASS: existing file"

if ./build/bincurl "127.0.0.1:$PORT/missing.txt" >"$TMP/missing.out"; then
  echo "FAIL: missing file returned success" >&2
  exit 1
fi
grep -q "not found" "$TMP/missing.out"
echo "PASS: 404 missing file"

./build/bincurl "127.0.0.1:$PORT/../index.html" >"$TMP/traversal.out" || true
grep -q "invalid path" "$TMP/traversal.out"
echo "PASS: traversal rejection"

# One TCP connection can carry two sequential requests.
python3 - "$PORT" <<'PY'
import socket, struct, zlib, sys
port=int(sys.argv[1])
s=socket.create_connection(("127.0.0.1", port))

def frame(t, flags, stream, payload):
    h=struct.pack(">HBBBBHII", 0x424E,1,t,flags,0,stream,len(payload),zlib.crc32(payload)&0xffffffff)
    return h+payload

def request(path):
    p=bytes([1])+struct.pack(">H",len(path))+path.encode()+bytes([0])
    s.sendall(frame(1,1,1,p))

def recv_frame():
    h=s.recv(16)
    while len(h)<16:
        h += s.recv(16-len(h))
    magic,ver,t,flags,res,stream,n,crc=struct.unpack(">HBBBBHII",h)
    p=b''
    while len(p)<n:
        p += s.recv(n-len(p))
    assert magic==0x424E and ver==1 and (zlib.crc32(p)&0xffffffff)==crc
    return t,flags,p

request("/hello.txt")
seen=[]
while True:
    t,f,p=recv_frame(); seen.append((t,f))
    if t==3 and f&1: break
assert seen[0][0]==2 and seen[-1][0]==3

request("/index.html")
while True:
    t,f,p=recv_frame()
    if t==3 and f&1: break
s.close()
print("PASS: two requests over one TCP connection")
PY

echo "All tests passed."

echo
echo "=============================="
echo "Protocol unit tests"
echo "=============================="

g++ \
    -std=c++17 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -O2 \
    -g \
    -Isrc \
    src/protocol.cpp \
    tests/protocol_tests.cpp \
    -o /tmp/protocol_tests

/tmp/protocol_tests

echo
echo "=============================="
echo "Integration tests"
echo "=============================="

./tests/malformed_tests.sh