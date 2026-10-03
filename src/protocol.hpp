#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace hbin {

constexpr uint16_t PROTOCOL_MAGIC = 0x424E;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t FRAME_HEADER_SIZE = 16;
constexpr uint32_t MAX_FRAME_PAYLOAD =1024 * 1024;

constexpr uint32_t MAX_PATH_LENGTH = 4096;
constexpr uint8_t MAX_HEADERS = 255;


// Every frame has exactly 16 bytes of header, followed by payload bytes.
// All integer fields are unsigned and encoded in network byte order (big endian).
struct FrameHeader {
    uint16_t magic;          // 0x424E = ASCII "BN"
    uint8_t version;         // protocol version, currently 1
    uint8_t type;            // frame type
    uint8_t flags;           // type-specific flags
    uint8_t reserved;        // must be zero
    uint16_t stream_id;      // stream identifier
    uint32_t payload_length;  // bytes after this header
    uint32_t payload_crc32;  // CRC-32 of payload
};
static_assert(sizeof(FrameHeader) == 16, "FrameHeader must be 16 bytes");

enum class FrameType : uint8_t {
    REQUEST = 1,
    RESPONSE_HEADERS = 2,
    DATA = 3,
    PING = 4,
    GOAWAY = 5,
};

enum FrameFlags : uint8_t {
    FLAG_NONE = 0,
    FLAG_END_STREAM = 1 << 0,
    FLAG_END_HEADERS = 1 << 1,
};

enum class Method : uint8_t {
    GET = 1,
};

struct Header {
    std::string name;
    std::string value;
};

struct Request {
    Method method;
    std::string path;
    std::vector<Header> headers;
};

struct ResponseHeaders {
    uint16_t status;
    std::vector<Header> headers;
};

struct Frame {
    FrameHeader header{};
    std::vector<uint8_t> payload;
};

uint32_t crc32(const std::vector<uint8_t>& data);

bool read_exact(int fd, void* buffer, size_t length);
bool write_all(int fd, const void* buffer, size_t length);

bool send_frame(int fd, FrameType type, uint8_t flags, uint16_t stream_id,
                const std::vector<uint8_t>& payload);

// Returns false on clean EOF or I/O/protocol failure. If the type is unknown,
// the function still reads the complete frame, allowing the caller to skip it.
bool recv_frame(int fd, Frame& frame);

std::vector<uint8_t> encode_request(const Request& request);
bool decode_request(const std::vector<uint8_t>& payload, Request& request,
                    std::string& error);

std::vector<uint8_t> encode_response_headers(const ResponseHeaders& response);
bool decode_response_headers(const std::vector<uint8_t>& payload,
                             ResponseHeaders& response, std::string& error);

std::string method_to_string(Method method);

} // namespace hbin
