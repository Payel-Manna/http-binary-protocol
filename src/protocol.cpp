#include "protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/socket.h>
#include <unistd.h>

namespace hbin {
namespace {

constexpr uint16_t MAGIC =  PROTOCOL_MAGIC;

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(v & 0xff));
}

bool get_u16(const std::vector<uint8_t>& in, size_t& pos, uint16_t& v) {
    if (pos + 2 > in.size()) return false;
    v = static_cast<uint16_t>((in[pos] << 8) | in[pos + 1]);
    pos += 2;
    return true;
}

bool get_bytes(const std::vector<uint8_t>& in, size_t& pos, size_t n,
               std::string& s) {
    if (n > in.size() - pos) return false;
    s.assign(reinterpret_cast<const char*>(in.data() + pos), n);
    pos += n;
    return true;
}

bool valid_header_name(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    for (unsigned char c : name) {
        if (!(c >= 'a' && c <= 'z') && c != '-' && !(c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}

bool valid_header_value(const std::string& value) {
    if (value.size() > 65535) return false;
    for (unsigned char c : value) {
        if (c == '\r' || c == '\n') return false;
    }
    return true;
}

bool encode_headers(const std::vector<Header>& headers, std::vector<uint8_t>& out) {
    if (headers.size() > 255) return false;
    out.push_back(static_cast<uint8_t>(headers.size()));
    for (const auto& h : headers) {
        if (!valid_header_name(h.name) || !valid_header_value(h.value)) return false;
        out.push_back(static_cast<uint8_t>(h.name.size()));
        out.insert(out.end(), h.name.begin(), h.name.end());
        put_u16(out, static_cast<uint16_t>(h.value.size()));
        out.insert(out.end(), h.value.begin(), h.value.end());
    }
    return true;
}

bool decode_headers(const std::vector<uint8_t>& in, size_t& pos,
                    std::vector<Header>& headers, std::string& error) {
    if (pos >= in.size()) { error = "missing header count"; return false; }
    uint8_t count = in[pos++];
    headers.clear();
    headers.reserve(count);

    for (uint8_t i = 0; i < count; ++i) {
        if (pos >= in.size()) { error = "missing header name length"; return false; }
        uint8_t name_len = in[pos++];
        if (name_len == 0 || pos + name_len > in.size()) {
            error = "invalid header name";
            return false;
        }
        std::string name;
        if (!get_bytes(in, pos, name_len, name) || !valid_header_name(name)) {
            error = "invalid header name";
            return false;
        }

        uint16_t value_len;
        if (!get_u16(in, pos, value_len) || pos + value_len > in.size()) {
            error = "invalid header value";
            return false;
        }
        std::string value;
        if (!get_bytes(in, pos, value_len, value) || !valid_header_value(value)) {
            error = "invalid header value";
            return false;
        }
        headers.push_back({std::move(name), std::move(value)});
    }
    return true;
}

} // namespace

uint32_t crc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool read_exact(int fd, void* buffer, size_t length) {
    auto* p = static_cast<uint8_t*>(buffer);
    size_t done = 0;
    while (done < length) {
        ssize_t n = ::recv(fd, p + done, length - done, 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

bool write_all(int fd, const void* buffer, size_t length) {
    const auto* p = static_cast<const uint8_t*>(buffer);
    size_t done = 0;
    while (done < length) {
        ssize_t n = ::send(fd, p + done, length - done, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

bool send_frame(int fd, FrameType type, uint8_t flags, uint16_t stream_id,
                const std::vector<uint8_t>& payload) {
    if (payload.size() > MAX_FRAME_PAYLOAD) return false;

    uint8_t raw[16]{};
    raw[0] = static_cast<uint8_t>((MAGIC >> 8) & 0xff);
    raw[1] = static_cast<uint8_t>(MAGIC & 0xff);
    raw[2] = PROTOCOL_VERSION;
    raw[3] = static_cast<uint8_t>(type);
    raw[4] = flags;
    raw[5] = 0;
    raw[6] = static_cast<uint8_t>((stream_id >> 8) & 0xff);
    raw[7] = static_cast<uint8_t>(stream_id & 0xff);

    uint32_t length = static_cast<uint32_t>(payload.size());
    uint32_t crc = crc32(payload);
    raw[8] = static_cast<uint8_t>((length >> 24) & 0xff);
    raw[9] = static_cast<uint8_t>((length >> 16) & 0xff);
    raw[10] = static_cast<uint8_t>((length >> 8) & 0xff);
    raw[11] = static_cast<uint8_t>(length & 0xff);
    raw[12] = static_cast<uint8_t>((crc >> 24) & 0xff);
    raw[13] = static_cast<uint8_t>((crc >> 16) & 0xff);
    raw[14] = static_cast<uint8_t>((crc >> 8) & 0xff);
    raw[15] = static_cast<uint8_t>(crc & 0xff);

    return write_all(fd, raw, sizeof(raw)) &&
           (payload.empty() || write_all(fd, payload.data(), payload.size()));
}

bool recv_frame(int fd, Frame& frame) {
    uint8_t raw[16];
    if (!read_exact(fd, raw, sizeof(raw))) return false;

    uint16_t magic = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    if (magic != MAGIC || raw[2] != PROTOCOL_VERSION || raw[5] != 0) return false;

    uint16_t stream_id = static_cast<uint16_t>((raw[6] << 8) | raw[7]);
    uint32_t length = (static_cast<uint32_t>(raw[8]) << 24) |
                      (static_cast<uint32_t>(raw[9]) << 16) |
                      (static_cast<uint32_t>(raw[10]) << 8) |
                      static_cast<uint32_t>(raw[11]);
    uint32_t expected_crc = (static_cast<uint32_t>(raw[12]) << 24) |
                            (static_cast<uint32_t>(raw[13]) << 16) |
                            (static_cast<uint32_t>(raw[14]) << 8) |
                            static_cast<uint32_t>(raw[15]);

    if (length > MAX_FRAME_PAYLOAD) return false;

    frame.header = {magic, raw[2], raw[3], raw[4], raw[5], stream_id,
                    length, expected_crc};
    frame.payload.resize(length);
    if (length > 0 && !read_exact(fd, frame.payload.data(), length)) return false;

    return crc32(frame.payload) == expected_crc;
}

std::vector<uint8_t> encode_request(const Request& request) {
    std::vector<uint8_t> out;
    if (request.path.empty() || request.path.size() > 65535) return {};
    out.push_back(static_cast<uint8_t>(request.method));
    put_u16(out, static_cast<uint16_t>(request.path.size()));
    out.insert(out.end(), request.path.begin(), request.path.end());
    if (!encode_headers(request.headers, out)) return {};
    return out;
}

bool decode_request(
    const std::vector<uint8_t>& payload,
    Request& request,
    std::string& error
) {
    size_t pos = 0;

    if (payload.size() < 3) {
        error = "request too short";
        return false;
    }

    // -----------------------------
    // Method
    // -----------------------------
    uint8_t method = payload[pos++];

    if (method != static_cast<uint8_t>(Method::GET)) {
        error = "unsupported method";
        return false;
    }

    // -----------------------------
    // Path length
    // -----------------------------
    uint16_t path_len = 0;

    if (!get_u16(payload, pos, path_len)) {
        error = "missing path length";
        return false;
    }

    if (path_len == 0) {
        error = "empty path";
        return false;
    }

    if (path_len > MAX_PATH_LENGTH) {
        error = "path too long";
        return false;
    }

    // -----------------------------
    // Path
    // -----------------------------
    if (
        path_len >
        payload.size() - pos
    ) {
        error = "invalid path";
        return false;
    }

    if (
        !get_bytes(
            payload,
            pos,
            path_len,
            request.path
        )
    ) {
        error = "invalid path";
        return false;
    }

    if (request.path.empty()) {
        error = "empty path";
        return false;
    }

    if (request.path[0] != '/') {
        error = "path must start with /";
        return false;
    }

    if (
        request.path.find('\0') !=
        std::string::npos
    ) {
        error = "NUL in path";
        return false;
    }

    // -----------------------------
    // Headers
    // -----------------------------
    if (
        !decode_headers(
            payload,
            pos,
            request.headers,
            error
        )
    ) {
        return false;
    }

    // -----------------------------
    // No trailing bytes
    // -----------------------------
    if (pos != payload.size()) {
        error = "trailing bytes in request";
        return false;
    }

    request.method =
        static_cast<Method>(method);

    return true;
}
std::vector<uint8_t> encode_response_headers(
    const ResponseHeaders& response
) {
    std::vector<uint8_t> out;

    put_u16(
        out,
        response.status
    );

    if (
        !encode_headers(
            response.headers,
            out
        )
    ) {
        return {};
    }

    return out;
}
bool decode_response_headers(const std::vector<uint8_t>& payload,
                             ResponseHeaders& response, std::string& error) {
    size_t pos = 0;
    if (!get_u16(payload, pos, response.status)) {
        error = "response too short";
        return false;
    }
    if (!decode_headers(payload, pos, response.headers, error)) return false;
    if (pos != payload.size()) {
        error = "trailing bytes in response headers";
        return false;
    }
    return true;
}

std::string method_to_string(Method method) {
    return method == Method::GET ? "GET" : "UNKNOWN";
}

} // namespace hbin
