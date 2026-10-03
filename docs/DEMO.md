# Demo / viva checklist

## 1. Start server

```bash
make
./build/serve ./www 9000
```

## 2. Successful request

In another terminal:

```bash
./build/bincurl -v 127.0.0.1:9000/index.html
```

Point out:

1. One TCP connection is created.
2. The request is a binary REQUEST frame.
3. The server returns RESPONSE_HEADERS followed by DATA.
4. `-v` prints the actual bytes crossing the socket.
5. The body is printed to stdout while protocol diagnostics go to stderr.

## 3. 404

```bash
./build/bincurl -v 127.0.0.1:9000/no-such-file.txt
printf 'exit=%s\n' "$?"
```

Expected status is 404 and exit status is non-zero.

## 4. Malformed request

Use the test suite to demonstrate malformed/traversal handling:

```bash
./tests/run_tests.sh
```

## 5. Persistent connection

The Python test sends two REQUEST frames on the same TCP socket. The server handles both without calling `accept()` a second time.

## 6. Unknown frame type

Explain that the fixed header's `payload_length` lets a receiver consume an unknown frame and continue at the next frame boundary. This is the required forward-compatibility rule.
