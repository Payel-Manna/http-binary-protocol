# Binary HTTP Protocol Specification v1

## 1. Purpose and transport

Binary HTTP (BHTTP) is a small HTTP-like protocol for the course project. It runs directly over a reliable, ordered TCP byte stream. The server listens on a configured TCP port; the client opens exactly one TCP connection for a command and sends requests on that connection. The server keeps the connection open after a response so another request can use the same connection.

The protocol uses no textual HTTP request line. The wire format is entirely binary except for UTF-8/ASCII strings carried inside length-prefixed fields.

All multi-byte integers are **unsigned big-endian (network byte order)**. A receiver must consume exactly the number of bytes declared by each frame's length field.

## 2. Frame format

Every frame begins with a **16-byte fixed-size header** followed by `payload_length` payload bytes.

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | magic | `0x424E`, ASCII `BN` |
| 2 | 1 | version | `1` |
| 3 | 1 | type | frame type number |
| 4 | 1 | flags | type-specific flags |
| 5 | 1 | reserved | MUST be `0` |
| 6 | 2 | stream_id | non-zero request/response stream; `0` only for connection control |
| 8 | 4 | payload_length | number of bytes after the 16-byte header |
| 12 | 4 | payload_crc32 | CRC-32 of payload bytes |

Maximum payload length is **1,048,576 bytes (1 MiB)**. This bounds memory use and makes a length field trustworthy enough to allocate safely.

### Why these fields?

- **Magic (16 bits):** rejects accidental connections/data from another protocol.
- **Version (8 bits):** permits a future incompatible version without silently interpreting it as v1.
- **Type (8 bits):** leaves 256 possible frame kinds while keeping the header fixed.
- **Flags (8 bits):** permits independent control bits without changing the frame layout.
- **Reserved (8 bits):** future extension space; v1 requires zero.
- **Stream ID (16 bits):** enough for 65,535 logical streams while keeping the header compact. This project uses stream `1` for ordinary client requests.
- **Length (32 bits):** makes framing unambiguous even though TCP itself is only a byte stream.
- **CRC-32 (32 bits):** detects corrupted payload data before the payload is interpreted.

## 3. Frame types

| Type | Name | Direction | Purpose |
|---:|---|---|---|
| 1 | REQUEST | client -> server | GET request |
| 2 | RESPONSE_HEADERS | server -> client | status + response headers |
| 3 | DATA | either | body bytes |
| 4 | PING | either | connection liveness test; payload is echoed |
| 5 | GOAWAY | either | graceful connection shutdown |

A receiver that sees an **unknown type MUST read the complete frame (header plus declared payload) and skip it**. It must not treat the payload as a new frame. This is the protocol's forward-compatibility rule.

A frame with bad magic, unsupported version, non-zero reserved field, length above the maximum, truncated bytes, or a CRC mismatch is malformed. The implementation may close the TCP connection because the byte stream can no longer be trusted for framing.

## 4. Flags

`FLAG_END_STREAM = 0x01` means no more DATA frames belong to this stream.

`FLAG_END_HEADERS = 0x02` means the RESPONSE_HEADERS frame contains the complete response-header block.

Unknown flag bits are ignored unless the frame type's future specification gives them meaning.

## 5. REQUEST payload

A REQUEST payload is:

| Width | Field |
|---:|---|
| 1 | method |
| 2 | path_length |
| path_length | path bytes |
| 1 | header_count |
| variable | header entries |

`method = 1` means GET. Version 1 supports GET only.

The path is an ASCII/UTF-8 byte string beginning with `/`. It is **not percent-decoded**. A server must reject a path containing a backslash or a `..` path component.

### Header entry

Each header is encoded as:

| Width | Field |
|---:|---|
| 1 | name_length |
| name_length | name bytes |
| 2 | value_length |
| value_length | value bytes |

There is one header-count byte followed by exactly that many entries. Header names are lowercase ASCII letters, digits, and `-`. Header values may contain ordinary bytes except CR and LF. A name is at most 255 bytes and a value is at most 65,535 bytes.

The reference client sends:

- `host`
- `accept`

The header count is therefore `2` for the normal client request. Other valid counts are allowed by the format.

A normal request has `FLAG_END_STREAM` set because it carries no request body.

## 6. RESPONSE_HEADERS payload

The payload begins with:

| Width | Field |
|---:|---|
| 2 | status |
| 1 | header_count |
| variable | header entries |

The status is an unsigned HTTP-style status code, for example `200`, `400`, `404`, and `500`.

The reference server sends at least:

- `content-type`
- `content-length`

The header block is terminated by `FLAG_END_HEADERS`.

## 7. DATA payload

A DATA frame contains only body bytes. The payload can be empty. The final DATA frame has `FLAG_END_STREAM` set.

The server splits large files into DATA frames no larger than 64 KiB even though the protocol permits 1 MiB. This keeps memory use bounded and demonstrates that a single TCP response can consist of multiple binary frames.

## 8. Connection workflow

1. Client creates one TCP socket.
2. Client connects to the server.
3. Client sends one REQUEST frame on stream `1`.
4. Server validates the frame and request.
5. Server safely maps the URL path below its configured root.
6. Server sends RESPONSE_HEADERS.
7. Server sends zero or more DATA frames.
8. Final DATA has END_STREAM.
9. The server does **not** close the connection merely because one request completed.
10. The same TCP connection can carry another REQUEST frame.
11. Client may close after receiving its requested response; it must never create a second connection for the same command.

## 9. Errors

- **400 Bad Request:** malformed frame/request, unsupported method, invalid path, wrong stream usage, or request not ending its stream.
- **404 Not Found:** requested path does not identify a regular file below the server root.
- **500 Internal Server Error:** server-side file/stat/open failure.

The client exits with a non-zero status for any 4xx or 5xx response.

## 10. Security and path mapping

The server treats its command-line root as the document root. It never concatenates a URL path and then blindly opens it. It rejects backslashes and `..` path components and checks that the normalized candidate remains below the normalized root.

The protocol deliberately does not implement URL percent-decoding; this avoids having two different path representations that could bypass the path checks.

## 11. Example wire exchange

For `GET /index.html`, the first bytes of the request are:

```text
42 4e       magic = BN
01          version 1
01          type REQUEST
01          END_STREAM
00          reserved
00 01       stream 1
00 00 00 1f payload length (example only)
...         CRC-32
...         binary request payload
```

The exact CRC and payload length depend on the actual request. The submitted annotated hexdump must therefore be generated from the student's own program, not copied from this example.
