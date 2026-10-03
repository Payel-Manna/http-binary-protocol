# Binary HTTP over TCP — C++ Implementation

A custom binary HTTP-style protocol implemented in C++17 over TCP sockets.

This project implements a complete client/server application consisting of:

- A binary application-layer protocol
- A persistent TCP server
- A command-line binary HTTP client
- Binary request and response serialization
- Fixed-size binary frame headers
- CRC32 payload integrity checking
- Static file serving
- HTTP-style status codes
- Path traversal protection
- Protocol validation and malformed-input handling
- A hexadecimal frame inspection utility
- Protocol unit tests
- End-to-end integration tests

The implementation is designed to demonstrate low-level networking, binary protocol design, serialization/deserialization, TCP stream handling, defensive parsing, and automated testing.

---

## 1. Project Overview

Traditional HTTP/1.x represents requests and responses primarily as text.

For example, a conceptual HTTP request looks like:

    GET /index.html HTTP/1.1
    Host: example.com
    Accept: */*

This project uses a custom binary protocol instead.

Requests and responses are represented as binary frames. Each frame contains a fixed-size 16-byte header followed by a variable-length payload.

The protocol therefore has the following general structure:

    +-------------------------------+
    |        Frame Header           |
    |          16 bytes             |
    +-------------------------------+
    |                               |
    |          Payload              |
    |       0 ... 1 MiB             |
    |                               |
    +-------------------------------+

The frame header provides enough information for the receiver to determine:

- Whether the frame belongs to this protocol
- Which protocol version is being used
- What kind of frame was received
- Which flags are set
- Which logical stream the frame belongs to
- How large the payload is
- Whether the payload passes the CRC32 integrity check

---

# 2. Main Components

The project consists of five main source components.

### `protocol.cpp` / `protocol.hpp`

Implements the binary protocol itself.

Responsibilities include:

- Frame encoding
- Frame decoding
- Request encoding
- Request decoding
- Response-header encoding
- Response-header decoding
- CRC32 calculation
- Reliable TCP reads
- Reliable TCP writes
- Frame validation

### `server.cpp`

Implements the TCP server.

Responsibilities include:

- Creating the listening socket
- Accepting TCP connections
- Receiving protocol frames
- Decoding requests
- Validating paths
- Serving files
- Generating response headers
- Sending response DATA frames
- Handling persistent connections
- Handling PING
- Handling GOAWAY

### `client.cpp`

Implements the command-line client.

Responsibilities include:

- Connecting to the server
- Constructing GET requests
- Encoding requests into binary frames
- Sending requests
- Receiving response frames
- Displaying status and response data
- Supporting multiple requests over one TCP connection

### `hexdump.cpp`

Provides a debugging utility that generates representative protocol frames and displays their raw bytes in hexadecimal and ASCII form.

### `tests/`

Contains automated tests for both the protocol layer and the complete client/server system.

---

# 3. Project Structure

    http_binary_course_project/
    │
    ├── src/
    │   ├── protocol.hpp
    │   ├── protocol.cpp
    │   ├── server.cpp
    │   ├── client.cpp
    │   └── hexdump.cpp
    │
    ├── tests/
    │   ├── protocol_tests.cpp
    │   ├── malformed_tests.sh
    │   └── run_tests.sh
    │
    ├── www/
    │   ├── index.html
    │   └── hello.txt
    │
    ├── docs/
    │   └── PROTOCOL.md
    │
    ├── build/
    │   ├── serve
    │   ├── bincurl
    │   └── hexdump
    │
    ├── Makefile
    ├── README.md
    └── .gitignore

---

# 4. Requirements

The project requires:

- Linux or another POSIX-compatible operating system
- GNU C++ compiler
- C++17 support
- GNU Make
- Python 3 for part of the integration-test suite

The project uses POSIX socket APIs for TCP networking.

The main compilation flags are:

    -std=c++17
    -Wall
    -Wextra
    -Wpedantic
    -O2
    -g

---

# 5. Building the Project

From the project root:

    make

This builds:

    build/serve
    build/bincurl
    build/hexdump

For a completely clean rebuild:

    make clean
    make

---

# 6. Running the Server

The server is started using:

    ./build/serve <document-root> <port>

Example:

    ./build/serve ./www 9000

The server will listen on TCP port 9000 and serve files from the `www` directory.

A successful startup produces a message similar to:

    httpbin server listening on port 9000 with root "..."

---

# 7. Running the Client

The client executable is:

    build/bincurl

Example:

    ./build/bincurl 127.0.0.1:9000/index.html

The client connects to the server, sends a binary GET request, receives the binary response, and displays the result.

Example interaction:

    Connected to 127.0.0.1:9000
    REQUEST 1: /index.html
    HTTP status: 200
    ...
    TCP connection closed.

The current client uses numeric IPv4 addresses. Therefore, use:

    127.0.0.1

when testing locally.

---

# 8. Multiple Requests on One TCP Connection

The client supports multiple URLs in one invocation.

Example:

    ./build/bincurl \
        127.0.0.1:9000/index.html \
        127.0.0.1:9000/hello.txt

The requests are sent over the same TCP connection.

The communication is conceptually:

    Client                         Server
      │                               │
      │──── TCP connection ──────────>│
      │                               │
      │──── REQUEST /index.html ─────>│
      │<─── RESPONSE HEADERS ─────────│
      │<─── DATA ─────────────────────│
      │                               │
      │──── REQUEST /hello.txt ──────>│
      │<─── RESPONSE HEADERS ─────────│
      │<─── DATA ─────────────────────│
      │                               │
      │──── TCP close ────────────────│

This persistent-connection behavior is verified by the integration tests.

---

# 9. Binary Protocol

The protocol uses a fixed 16-byte frame header.

Each frame has:

    +-------------------------------+
    |         Frame Header          |
    |            16 bytes           |
    +-------------------------------+
    |            Payload            |
    |         variable size         |
    +-------------------------------+

The maximum payload size is:

    1 MiB

The frame header contains:

| Field | Size |
|---|---:|
| Magic | 2 bytes |
| Version | 1 byte |
| Type | 1 byte |
| Flags | 1 byte |
| Reserved | 1 byte |
| Stream ID | 2 bytes |
| Payload Length | 4 bytes |
| Payload CRC32 | 4 bytes |

Total:

    16 bytes

All multi-byte integers are encoded in network byte order (big-endian).

A complete byte-level description is provided in:

    docs/PROTOCOL.md

---

# 10. Frame Types

The protocol defines the following frame types:

    REQUEST = 1
    RESPONSE_HEADERS = 2
    DATA = 3
    PING = 4
    GOAWAY = 5

### REQUEST

Carries a client request.

### RESPONSE_HEADERS

Carries the response status and response headers.

### DATA

Carries response body data.

### PING

Provides protocol-level ping/echo functionality.

### GOAWAY

Indicates that protocol processing on a connection is being terminated.

---

# 11. Frame Flags

The protocol defines:

    FLAG_NONE       = 0
    FLAG_END_STREAM = 1 << 0
    FLAG_END_HEADERS = 1 << 1

`END_STREAM` indicates that the current stream is complete.

`END_HEADERS` indicates that a header block is complete.

---

# 12. Stream IDs

Each frame contains a 16-bit stream identifier.

For example:

    Stream 1
    Stream 2
    Stream 3

The client assigns stream identifiers to requests when multiple requests are sent over a persistent connection.

The stream identifier allows the protocol to associate frames with a logical request/response exchange.

---

# 13. CRC32 Integrity Checking

Every frame stores a CRC32 checksum of its payload.

When sending:

    payload
       │
       ▼
     CRC32
       │
       ▼
    frame header

When receiving:

    received payload
          │
          ▼
       CRC32
          │
          ▼
      compare with
      header CRC32

If the values differ, the frame is rejected.

This protects against undetected payload corruption.

The unit-test suite includes an explicit corrupted-CRC test.

---

# 14. Request Format

The currently supported request method is:

    GET

A request contains:

- Method
- Path
- Request headers

The binary request payload is structured as:

    +-------------------+
    | Method            |
    |      1 byte       |
    +-------------------+
    | Path Length       |
    |      2 bytes      |
    +-------------------+
    | Path              |
    | variable length   |
    +-------------------+
    | Header Count      |
    |      1 byte       |
    +-------------------+
    | Headers           |
    | variable length   |
    +-------------------+

For example, the conceptual request:

    GET /index.html
    Host: localhost
    Accept: */*

is serialized into a binary REQUEST frame.

---

# 15. Request Validation

The protocol performs validation before accepting a request.

Validation includes:

- Minimum payload size
- Supported method
- Non-empty path
- Maximum path length
- Path beginning with `/`
- Rejection of embedded NUL bytes
- Valid header count
- Valid header names
- Valid header values
- Rejection of trailing bytes

Malformed requests are rejected instead of being processed as valid requests.

---

# 16. Header Validation

Header names are restricted to lowercase letters, digits, and `-`.

For example:

    content-type

is valid.

A name containing uppercase characters such as:

    Content-Type

is rejected by the protocol parser.

Header values are also checked for prohibited carriage-return and line-feed characters.

---

# 17. Response Format

A response begins with a `RESPONSE_HEADERS` frame.

The response contains:

- Status code
- Response headers

The response body is then transmitted using one or more DATA frames.

Conceptually:

    RESPONSE_HEADERS
           │
           ▼
        DATA
           │
           ▼
        DATA
           │
           ▼
    DATA + END_STREAM

This allows response bodies to be transmitted in chunks.

---

# 18. HTTP-Style Status Codes

The server uses HTTP-style status codes.

Important responses include:

| Status | Meaning |
|---:|---|
| 200 | Request succeeded |
| 400 | Bad request |
| 404 | Requested file was not found |
| 500 | Internal server/filesystem error |

Example:

    /index.html

returns:

    HTTP status: 200

while a missing resource such as:

    /does-not-exist.txt

returns:

    HTTP status: 404

---

# 19. Static File Serving

The server is configured with a document root.

For example:

    ./build/serve ./www 9000

sets:

    ./www

as the document root.

A request for:

    /index.html

is resolved relative to that root.

The server reads the requested regular file and sends the contents as DATA frames.

---

# 20. Path Traversal Protection

The server validates requested paths before accessing the filesystem.

Unsafe path traversal attempts such as:

    /../index.html

are rejected.

The path resolution logic also checks for:

- Backslash characters
- `..` path components
- Invalid absolute/relative path combinations
- Paths escaping the configured document root

This prevents a client from using the request path to access files outside the server's document root.

---

# 21. Content-Type Handling

The server determines the response content type from the requested file extension.

Examples include:

    .html → text/html
    .txt  → text/plain
    .css  → text/css
    .js   → application/javascript

The detected content type is returned as a response header.

---

# 22. Persistent TCP Connections

The server processes multiple requests on a single TCP connection.

The basic server loop is:

    accept connection
          │
          ▼
    receive frame
          │
          ▼
    decode request
          │
          ▼
    validate request
          │
          ▼
    access requested file
          │
          ▼
    send response
          │
          ▼
    receive next frame
          │
          ▼
         ...

The connection remains open until the peer closes it or a protocol-level termination condition occurs.

---

# 23. TCP Read and Write Handling

TCP is a byte stream. A single `read()` call is not guaranteed to return all requested bytes.

The protocol therefore provides:

    read_exact()

and:

    write_all()

These helpers continue reading or writing until the requested number of bytes has been transferred or an error occurs.

This is particularly important when receiving the 16-byte frame header.

---

# 24. Error Handling

The frame decoder validates:

- Magic value
- Protocol version
- Reserved field
- Payload length
- Payload CRC32

The request decoder validates:

- Request length
- Method
- Path
- Headers
- Trailing bytes

The server additionally validates:

- Stream ID
- Request termination
- Filesystem path
- File existence
- File type

Invalid conditions result in rejection or an appropriate error response.

---

# 25. Hexdump Utility

The project contains a protocol inspection utility:

    ./build/hexdump

It creates representative frames and prints their bytes in hexadecimal.

The output contains:

- Byte offset
- Hexadecimal representation
- ASCII representation

This makes it possible to inspect the actual binary representation of protocol messages.

---

# 26. Testing

The project includes both unit tests and integration tests.

### Protocol unit tests

    tests/protocol_tests.cpp

These test:

- CRC32
- Request serialization
- Request parsing
- Response serialization
- Response parsing
- Header validation
- Path validation
- Malformed input
- Frame transmission
- Invalid magic
- Invalid version
- Invalid CRC
- Invalid reserved field

### Integration tests

    tests/malformed_tests.sh
    tests/run_tests.sh

These test:

- Existing file
- Missing file
- Path traversal rejection
- Persistent TCP connections
- End-to-end client/server communication

---

# 27. Running Tests

The complete test suite can be run using:

    make test

The test process builds the project and executes the protocol and integration tests.

A successful run reports:

    All protocol unit tests passed.

and:

    ALL INTEGRATION TESTS PASSED

---

# 28. Manual Testing

Start the server:

    ./build/serve ./www 9000

In another terminal:

    ./build/bincurl 127.0.0.1:9000/index.html

Test a missing file:

    ./build/bincurl 127.0.0.1:9000/does-not-exist.txt

Test multiple requests:

    ./build/bincurl \
        127.0.0.1:9000/index.html \
        127.0.0.1:9000/hello.txt

Test the hexdump:

    ./build/hexdump

---

# 29. Clean Build Before Submission

Before final submission, run:

    make clean
    make
    make test

This ensures that all executables are generated from the current source code and that the complete automated test suite passes.

---

# 30. Design Principles

The implementation follows several design principles.

### Explicit serialization

Protocol fields are explicitly converted to and from bytes instead of relying on compiler-specific struct serialization.

### Fixed frame header

Every frame has a predictable 16-byte header.

### Length-delimited payloads

The header specifies exactly how many payload bytes belong to the frame.

### Integrity checking

CRC32 allows corrupted payloads to be detected.

### Defensive parsing

Malformed data is rejected before it is interpreted as a valid request or response.

### Persistent connections

Multiple requests can use the same TCP connection.

### Filesystem isolation

Requested paths are restricted to the configured document root.

### Automated testing

Both protocol-level and end-to-end behavior are tested.

---

# 31. Current Scope

The current implementation intentionally provides a focused protocol.

The supported request method is:

    GET

The client currently uses numeric IPv4 addresses.

The protocol is a custom binary application-layer protocol and is not intended to be wire-compatible with standard HTTP/1.x or HTTP/2.

The primary purpose of the implementation is to demonstrate:

- TCP socket programming
- Binary protocol design
- Framing
- Serialization and deserialization
- CRC32 integrity checking
- Persistent connections
- Static file serving
- Defensive parsing
- Automated testing

---

# 32. Quick Command Reference

Build:

    make

Clean:

    make clean

Build and test:

    make test

Start server:

    ./build/serve ./www 9000

Request file:

    ./build/bincurl 127.0.0.1:9000/index.html

Multiple requests:

    ./build/bincurl \
        127.0.0.1:9000/index.html \
        127.0.0.1:9000/hello.txt

Run hexdump:

    ./build/hexdump

---

# 33. Final Submission Checklist

Before submission:

    [ ] Clean build succeeds
    [ ] Complete test suite passes
    [ ] Server starts correctly
    [ ] Client retrieves an existing file
    [ ] Missing file returns 404
    [ ] Path traversal is rejected
    [ ] Persistent connection works
    [ ] Protocol unit tests pass
    [ ] Integration tests pass
    [ ] Hexdump utility works
    [ ] README.md is included
    [ ] docs/PROTOCOL.md is included
    [ ] Unnecessary backup files are removed
    [ ] No unnecessary temporary files are included

---

# 34. Conclusion

This project implements a complete binary HTTP-style client/server system over TCP using C++17.

The system combines:

    TCP networking
          +
    Binary framing
          +
    CRC32 integrity
          +
    Request/response serialization
          +
    Persistent connections
          +
    Static file serving
          +
    Path validation
          +
    Automated testing

The resulting implementation demonstrates both low-level network programming and application-layer protocol design.