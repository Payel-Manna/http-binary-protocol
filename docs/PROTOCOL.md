# Binary HTTP Protocol Specification

## Version 1

This document specifies the binary application-layer protocol implemented by the project.

The protocol operates over a reliable TCP byte stream.

---

# 1. Protocol Identifier

The protocol uses the following magic value:

    0x424E

The ASCII interpretation is:

    B N

The magic value is encoded as a 16-bit unsigned integer in network byte order.

The protocol version is:

    1

---

# 2. Transport

The protocol operates over:

    TCP

TCP provides:

- Reliable byte delivery
- Ordered delivery
- Retransmission
- Connection-oriented communication

The application protocol therefore does not need to implement packet ordering or retransmission itself.

---

# 3. Frame Structure

All application messages are transmitted as frames.

Each frame consists of:

    Frame Header
    Payload

The frame header is exactly 16 bytes.

General layout:

    +-------------------------------+
    |        Frame Header           |
    |          16 bytes             |
    +-------------------------------+
    |                               |
    |          Payload              |
    |       variable length         |
    |                               |
    +-------------------------------+

---

# 4. Frame Header

The frame header has the following layout:

    Offset  Size    Field
    ------  ------  ----------------
       0      2     Magic
       2      1     Version
       3      1     Type
       4      1     Flags
       5      1     Reserved
       6      2     Stream ID
       8      4     Payload Length
      12      4     Payload CRC32

Total:

    16 bytes

All multi-byte integer fields use network byte order (big-endian).

---

# 5. Magic

The magic field is:

    0x424E

It occupies bytes 0 and 1.

For a valid frame:

    byte 0 = 0x42
    byte 1 = 0x4E

A frame with another magic value is rejected.

---

# 6. Version

The protocol version occupies one byte.

Current value:

    0x01

A frame containing another version is rejected.

This allows future protocol versions to be distinguished from version 1.

---

# 7. Frame Type

The frame type occupies one byte.

The defined values are:

    1 = REQUEST
    2 = RESPONSE_HEADERS
    3 = DATA
    4 = PING
    5 = GOAWAY

---

# 8. Flags

The flags field occupies one byte.

Defined flags:

    0x00 = FLAG_NONE
    0x01 = FLAG_END_STREAM
    0x02 = FLAG_END_HEADERS

Flags may be combined using bitwise OR where appropriate.

For example:

    FLAG_END_STREAM | FLAG_END_HEADERS

would produce:

    0x03

---

# 9. Reserved Field

The reserved field occupies one byte.

Version 1 requires:

    Reserved = 0

A non-zero reserved field is rejected.

The field exists to preserve space for future protocol extensions.

---

# 10. Stream ID

The stream identifier occupies two bytes.

It is an unsigned 16-bit integer.

Example:

    Stream ID = 1

is encoded as:

    00 01

The stream ID identifies the logical request/response exchange associated with the frame.

---

# 11. Payload Length

The payload length occupies four bytes.

It specifies exactly how many bytes follow the 16-byte header.

The maximum permitted payload length is:

    1 MiB
    1,048,576 bytes

A frame exceeding the configured maximum is rejected.

---

# 12. Payload CRC32

The final four bytes of the frame header contain the CRC32 checksum of the payload.

The checksum is calculated over:

    payload bytes only

It does not include the frame header.

The receiver calculates CRC32 over the received payload and compares the result with the value stored in the header.

If they differ, the frame is considered invalid.

---

# 13. Frame Reception Procedure

A receiver processes a frame as follows:

    1. Read exactly 16 bytes.
    2. Decode the frame header.
    3. Validate the magic value.
    4. Validate the protocol version.
    5. Validate the reserved field.
    6. Validate the payload length.
    7. Read exactly Payload Length bytes.
    8. Calculate CRC32 of the payload.
    9. Compare calculated CRC32 with header CRC32.
   10. Accept the frame if all checks succeed.

Any failed validation causes the frame to be rejected.

---

# 14. REQUEST Frame

A REQUEST frame has:

    Type = 1

The request payload contains:

    Method
    Path Length
    Path
    Header Count
    Headers

---

# 15. Request Payload Layout

The request payload is:

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

---

# 16. Method

The method occupies one byte.

Currently:

    0x01 = GET

The implementation supports GET requests.

An unsupported method causes request decoding to fail.

---

# 17. Path Length

The path length is an unsigned 16-bit value.

It specifies the number of bytes in the path.

Example:

    /index.html

has length:

    11

The path length is encoded in big-endian order.

The implementation also imposes a maximum path length.

---

# 18. Path

The path is encoded using its exact byte length.

A valid path must:

- Be non-empty
- Begin with `/`
- Not contain NUL
- Not exceed the configured maximum length

Example:

    /index.html

is valid.

Example:

    index.html

is rejected because it does not begin with `/`.

---

# 19. Header Count

The header count occupies one byte.

Therefore the maximum number of encoded headers is:

    255

---

# 20. Header Encoding

Each header contains:

    Header Name Length
    Header Name
    Header Value Length
    Header Value

Conceptually:

    +-------------------+
    | Name Length       |
    |      2 bytes      |
    +-------------------+
    | Name              |
    | variable length   |
    +-------------------+
    | Value Length      |
    |      2 bytes      |
    +-------------------+
    | Value             |
    | variable length   |
    +-------------------+

---

# 21. Header Name Rules

Header names must:

- Be non-empty
- Contain only lowercase letters
- May contain digits
- May contain `-`
- Fit within the configured maximum size

Examples:

    host
    accept
    content-type
    content-length

are valid forms.

An uppercase character is rejected.

---

# 22. Header Value Rules

Header values:

- Must fit within the configured maximum size
- Must not contain carriage return
- Must not contain line feed

This prevents malformed header representations.

---

# 23. Request Termination

A complete request frame uses:

    FLAG_END_STREAM

when the request payload contains the complete request.

The server expects a complete request before processing it.

---

# 24. RESPONSE_HEADERS Frame

A response-header frame has:

    Type = 2

Its payload contains:

    Status
    Headers

---

# 25. Response Header Payload

The layout is:

    +-------------------+
    | Status            |
    |      2 bytes      |
    +-------------------+
    | Header Count      |
    |      1 byte       |
    +-------------------+
    | Headers           |
    | variable length   |
    +-------------------+

The status is an unsigned 16-bit value.

The header encoding follows the same general name/value format used for request headers.

---

# 26. Response Status

The response status occupies two bytes.

Examples:

    200
    400
    404
    500

The status is encoded in network byte order.

---

# 27. Response Header Termination

The response header frame uses:

    FLAG_END_HEADERS

to indicate that the response header block is complete.

---

# 28. DATA Frame

A DATA frame has:

    Type = 3

Its payload contains application response data.

For example, a text file may be transmitted as:

    DATA
    "Hello from Binary HTTP"

If the DATA frame contains the final bytes of the response body, it uses:

    FLAG_END_STREAM

---

# 29. Large Response Bodies

Response bodies may be divided across multiple DATA frames.

Example:

    RESPONSE_HEADERS
          │
          ▼
    DATA frame
          │
          ▼
    DATA frame
          │
          ▼
    DATA frame
          │
          ▼
    DATA + END_STREAM

This prevents the protocol from requiring an entire response body to fit inside one frame.

---

# 30. PING Frame

A PING frame has:

    Type = 4

The server recognizes PING frames and responds using the corresponding protocol mechanism.

PING is useful for checking whether a connection is responsive.

---

# 31. GOAWAY Frame

A GOAWAY frame has:

    Type = 5

Receiving GOAWAY causes the connection-processing loop to terminate.

It provides a protocol-level indication that the connection is being shut down.

---

# 32. Persistent Connections

A TCP connection may contain multiple request/response exchanges.

Example:

    TCP connection
        │
        ├── REQUEST stream 1
        ├── RESPONSE_HEADERS stream 1
        ├── DATA stream 1
        │
        ├── REQUEST stream 2
        ├── RESPONSE_HEADERS stream 2
        ├── DATA stream 2
        │
        └── connection close

The server therefore does not have to establish a new TCP connection for every request.

---

# 33. TCP Byte-Stream Handling

TCP does not preserve application-level message boundaries.

Therefore a call such as:

    read(fd, buffer, 16)

is not guaranteed to return exactly 16 bytes.

The implementation uses:

    read_exact()

to repeatedly read until the requested number of bytes has been obtained.

Similarly:

    write_all()

ensures that all requested bytes are written.

This provides the framing layer with reliable fixed-length header reads and complete frame writes.

---

# 34. Filesystem Mapping

The server is configured with a document root.

For example:

    ./build/serve ./www 9000

sets:

    ./www

as the document root.

A request:

    /index.html

is mapped to the corresponding file under the document root.

---

# 35. Path Security

The server rejects unsafe paths.

The path validation prevents:

    ..
    
path components from being used to escape the document root.

Backslash characters are also rejected.

The final resolved path must remain inside the configured document root.

---

# 36. Error Responses

The server uses the following status codes for common conditions:

    200
    Successful request

    400
    Invalid request or invalid path

    404
    Requested resource does not exist

    500
    Server-side filesystem or internal error

---

# 37. Invalid Frame Handling

The frame decoder rejects frames with:

- Incorrect magic
- Incorrect version
- Non-zero reserved byte
- Excessive payload length
- Invalid CRC32
- Truncated header
- Truncated payload

The protocol test suite contains explicit tests for several of these conditions.

---

# 38. Invalid Request Handling

The request decoder rejects:

- Empty payload
- Unsupported method
- Missing path
- Path without `/`
- NUL in path
- Invalid header names
- Invalid header values
- Truncated request
- Unexpected trailing bytes

---

# 39. Invalid Response Handling

The response decoder rejects:

- Truncated status
- Invalid header encoding
- Invalid header names
- Invalid header values
- Unexpected trailing bytes

---

# 40. Protocol Limits

Version 1 uses the following limits:

    Maximum frame payload:
        1 MiB

    Maximum path length:
        4096 bytes

    Maximum number of headers:
        255

    Maximum header name length:
        255 bytes

    Maximum header value length:
        65535 bytes

---

# 41. Endianness

All multi-byte integers are encoded in network byte order.

For example:

    uint16_t value = 0x1234

is represented as:

    12 34

and:

    uint32_t value = 0x12345678

is represented as:

    12 34 56 78

This ensures a deterministic wire representation.

---

# 42. Example Request

Consider:

    GET /index.html
    host: localhost
    accept: */*

The protocol generates:

    REQUEST frame
        │
        ├── Magic
        ├── Version
        ├── Type = REQUEST
        ├── Flags = END_STREAM
        ├── Stream ID
        ├── Payload Length
        ├── Payload CRC32
        │
        └── Payload
              ├── Method = GET
              ├── Path Length
              ├── "/index.html"
              ├── Header Count
              ├── host: localhost
              └── accept: */*

---

# 43. Example Successful Response

For an existing file:

    RESPONSE_HEADERS
        │
        ├── Status = 200
        ├── content-type
        └── content-length
                │
                ▼
             DATA
                │
                ▼
        DATA + END_STREAM

---

# 44. Example Missing File Response

If the requested file does not exist:

    RESPONSE_HEADERS
        │
        ├── Status = 404
        ├── content-type: text/plain
        └── content-length
                │
                ▼
             DATA
                │
                ▼
        DATA + END_STREAM

---

# 45. Protocol Processing Summary

The complete request/response process is:

    Client
      │
      │ TCP connect
      ▼
    Server
      │
      │ REQUEST frame
      ▼
    Frame decoder
      │
      │ validate header
      │ validate CRC
      ▼
    Request decoder
      │
      │ validate method/path/headers
      ▼
    Filesystem resolver
      │
      ├── invalid path ────────> 400
      │
      ├── missing file ────────> 404
      │
      └── valid file
              │
              ▼
         Response encoder
              │
              ├── RESPONSE_HEADERS
              │
              └── DATA frames
                        │
                        ▼
                      Client

---

# 46. Versioning

The version field currently contains:

    1

Future versions can use a different version number.

A version-1 implementation does not accept a frame with an unsupported version.

This prevents a newer or incompatible wire format from being silently interpreted as version 1.

---

# 47. Protocol Design Goals

The protocol is designed around:

1. Explicit framing
2. Deterministic binary representation
3. Length-delimited payloads
4. Payload integrity verification
5. Defensive parsing
6. Persistent TCP connections
7. Simple request/response semantics
8. Safe filesystem access
9. Easy byte-level inspection
10. Automated verification

---

# 48. Reference Implementation

The reference implementation for this specification is contained in:

    src/protocol.hpp
    src/protocol.cpp
    src/server.cpp
    src/client.cpp

The protocol behavior is verified by:

    tests/protocol_tests.cpp
    tests/malformed_tests.sh
    tests/run_tests.sh

---