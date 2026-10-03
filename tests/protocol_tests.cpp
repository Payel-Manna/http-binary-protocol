#include "../src/protocol.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

using namespace hbin;


// ============================================================
// FRAME-LEVEL TESTS
// ============================================================

static void test_valid_frame()
{
    int sockets[2];

    int result = socketpair(
        AF_UNIX,
        SOCK_STREAM,
        0,
        sockets
    );

    assert(result == 0);

    std::vector<uint8_t> payload = {
        'H', 'e', 'l', 'l', 'o'
    };

    bool sent = send_frame(
        sockets[0],
        FrameType::DATA,
        FLAG_END_STREAM,
        1,
        payload
    );

    assert(sent);

    Frame frame;

    bool received = recv_frame(
        sockets[1],
        frame
    );

    assert(received);

    assert(frame.header.magic == PROTOCOL_MAGIC);
    assert(frame.header.version == PROTOCOL_VERSION);

    assert(
        frame.header.type ==
        static_cast<uint8_t>(FrameType::DATA)
    );

    assert(frame.header.flags == FLAG_END_STREAM);
    assert(frame.header.reserved == 0);
    assert(frame.header.stream_id == 1);
    assert(frame.header.payload_length == payload.size());

    assert(frame.payload == payload);

    close(sockets[0]);
    close(sockets[1]);

    std::cout << "PASS: valid frame send/receive\n";
}


static void test_bad_crc()
{
    int sockets[2];

    int result = socketpair(
        AF_UNIX,
        SOCK_STREAM,
        0,
        sockets
    );

    assert(result == 0);

    std::vector<uint8_t> payload = {
        'H', 'e', 'l', 'l', 'o'
    };

    uint32_t crc = crc32(payload);

    uint8_t header[16]{};

    header[0] =
        static_cast<uint8_t>(PROTOCOL_MAGIC >> 8);

    header[1] =
        static_cast<uint8_t>(PROTOCOL_MAGIC);

    header[2] = PROTOCOL_VERSION;

    header[3] =
        static_cast<uint8_t>(FrameType::DATA);

    header[4] = FLAG_END_STREAM;
    header[5] = 0;

    header[6] = 0;
    header[7] = 1;

    uint32_t length =
        static_cast<uint32_t>(payload.size());

    header[8]  = static_cast<uint8_t>(length >> 24);
    header[9]  = static_cast<uint8_t>(length >> 16);
    header[10] = static_cast<uint8_t>(length >> 8);
    header[11] = static_cast<uint8_t>(length);

    // Intentionally corrupt the CRC.
    crc ^= 0xFFFFFFFFu;

    header[12] = static_cast<uint8_t>(crc >> 24);
    header[13] = static_cast<uint8_t>(crc >> 16);
    header[14] = static_cast<uint8_t>(crc >> 8);
    header[15] = static_cast<uint8_t>(crc);

    assert(
        write_all(
            sockets[0],
            header,
            sizeof(header)
        )
    );

    assert(
        write_all(
            sockets[0],
            payload.data(),
            payload.size()
        )
    );

    Frame frame;

    bool received = recv_frame(
        sockets[1],
        frame
    );

    assert(!received);

    close(sockets[0]);
    close(sockets[1]);

    std::cout << "PASS: bad CRC rejected\n";
}


static void test_bad_magic()
{
    int sockets[2];

    assert(
        socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets
        ) == 0
    );

    uint8_t header[16]{};

    // Wrong magic.
    header[0] = 0x12;
    header[1] = 0x34;

    header[2] = PROTOCOL_VERSION;

    header[3] =
        static_cast<uint8_t>(FrameType::DATA);

    header[4] = FLAG_END_STREAM;
    header[5] = 0;

    header[6] = 0;
    header[7] = 1;

    assert(
        write_all(
            sockets[0],
            header,
            sizeof(header)
        )
    );

    Frame frame;

    bool received = recv_frame(
        sockets[1],
        frame
    );

    assert(!received);

    close(sockets[0]);
    close(sockets[1]);

    std::cout << "PASS: bad magic rejected\n";
}


static void test_bad_version()
{
    int sockets[2];

    assert(
        socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets
        ) == 0
    );

    uint8_t header[16]{};

    header[0] =
        static_cast<uint8_t>(PROTOCOL_MAGIC >> 8);

    header[1] =
        static_cast<uint8_t>(PROTOCOL_MAGIC);

    // Wrong protocol version.
    header[2] = 99;

    header[3] =
        static_cast<uint8_t>(FrameType::DATA);

    header[4] = FLAG_END_STREAM;
    header[5] = 0;

    header[6] = 0;
    header[7] = 1;

    assert(
        write_all(
            sockets[0],
            header,
            sizeof(header)
        )
    );

    Frame frame;

    bool received = recv_frame(
        sockets[1],
        frame
    );

    assert(!received);

    close(sockets[0]);
    close(sockets[1]);

    std::cout << "PASS: bad version rejected\n";
}


static void test_reserved_byte()
{
    int sockets[2];

    assert(
        socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets
        ) == 0
    );

    uint8_t header[16]{};

    header[0] =
        static_cast<uint8_t>(PROTOCOL_MAGIC >> 8);

    header[1] =
        static_cast<uint8_t>(PROTOCOL_MAGIC);

    header[2] = PROTOCOL_VERSION;

    header[3] =
        static_cast<uint8_t>(FrameType::DATA);

    header[4] = FLAG_END_STREAM;

    // Reserved byte must be zero.
    header[5] = 1;

    header[6] = 0;
    header[7] = 1;

    assert(
        write_all(
            sockets[0],
            header,
            sizeof(header)
        )
    );

    Frame frame;

    bool received = recv_frame(
        sockets[1],
        frame
    );

    assert(!received);

    close(sockets[0]);
    close(sockets[1]);

    std::cout << "PASS: reserved byte rejected\n";
}


// ============================================================
// CRC TESTS
// ============================================================

static void test_crc()
{
    std::vector<uint8_t> data1 = {
        'H', 'e', 'l', 'l', 'o'
    };

    std::vector<uint8_t> data2 = {
        'H', 'e', 'l', 'l', 'p'
    };

    uint32_t crc1 = crc32(data1);
    uint32_t crc2 = crc32(data1);
    uint32_t crc3 = crc32(data2);

    // Same data must produce the same CRC.
    assert(crc1 == crc2);

    // Changing the data should change the CRC.
    assert(crc1 != crc3);

    std::cout << "PASS: CRC tests\n";
}


// ============================================================
// REQUEST TESTS
// ============================================================

static void test_request_encode_decode()
{
    Request request;

    request.method = Method::GET;
    request.path = "/index.html";

    request.headers = {
        {"host", "localhost"},
        {"accept", "*/*"}
    };

    auto encoded = encode_request(request);

    assert(!encoded.empty());

    Request decoded;
    std::string error;

    bool ok = decode_request(
        encoded,
        decoded,
        error
    );

    assert(ok);

    assert(decoded.method == Method::GET);
    assert(decoded.path == "/index.html");

    assert(decoded.headers.size() == 2);

    assert(decoded.headers[0].name == "host");
    assert(decoded.headers[0].value == "localhost");

    assert(decoded.headers[1].name == "accept");
    assert(decoded.headers[1].value == "*/*");

    std::cout << "PASS: request encode/decode\n";
}


static void test_empty_path()
{
    Request request;

    request.method = Method::GET;
    request.path = "";

    auto encoded = encode_request(request);

    // Empty paths must not be encoded as valid requests.
    assert(encoded.empty());

    std::cout << "PASS: empty path rejected\n";
}


static void test_path_without_slash()
{
    Request request;

    request.method = Method::GET;
    request.path = "index.html";

    auto encoded = encode_request(request);

    /*
     * encode_request() only checks that the path is non-empty.
     * Therefore decode_request() must perform the path validation.
     */
    assert(!encoded.empty());

    Request decoded;
    std::string error;

    bool ok = decode_request(
        encoded,
        decoded,
        error
    );

    assert(!ok);
    assert(error == "path must start with /");

    std::cout << "PASS: path without / rejected\n";
}


static void test_nul_in_path()
{
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(Method::GET),

        0x00,
        0x05,

        '/',
        'a',
        '\0',
        'b',
        'c',

        // zero headers
        0x00
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);
    assert(error == "NUL in path");

    std::cout << "PASS: NUL in path rejected\n";
}


static void test_unsupported_method()
{
    std::vector<uint8_t> payload = {
        0x02,       // unsupported method

        0x00,
        0x01,

        '/',

        0x00        // zero headers
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);
    assert(error == "unsupported method");

    std::cout << "PASS: unsupported method rejected\n";
}


static void test_truncated_request()
{
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(Method::GET),
        0x00
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);

    std::cout << "PASS: truncated request rejected\n";
}


static void test_trailing_request_bytes()
{
    Request request;

    request.method = Method::GET;
    request.path = "/index.html";

    auto encoded = encode_request(request);

    assert(!encoded.empty());

    // Add an unexpected byte after the valid request.
    encoded.push_back(0xFF);

    Request decoded;
    std::string error;

    bool ok = decode_request(
        encoded,
        decoded,
        error
    );

    assert(!ok);
    assert(error == "trailing bytes in request");

    std::cout << "PASS: trailing request bytes rejected\n";
}


// ============================================================
// HEADER TESTS
// ============================================================

static void test_malformed_headers()
{
    /*
     * GET /
     *
     * Header count = 1
     * Header name length = 0
     */
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(Method::GET),

        0x00,
        0x01,

        '/',

        0x01,       // one header
        0x00        // zero-length name
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);

    std::cout << "PASS: malformed headers rejected\n";
}


static void test_invalid_header_name()
{
    /*
     * Header name contains uppercase 'H'.
     * Header names are restricted by protocol.cpp to lowercase
     * letters, digits and '-'.
     */
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(Method::GET),

        0x00,
        0x01,

        '/',

        0x01,       // one header

        0x01,       // name length
        'H',

        0x00,
        0x01,
        'x'
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);

    std::cout << "PASS: invalid header name rejected\n";
}


static void test_invalid_header_value()
{
    /*
     * Header value contains '\n', which is prohibited.
     */
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(Method::GET),

        0x00,
        0x01,

        '/',

        0x01,       // one header

        0x04,
        't', 'e', 's', 't',

        0x00,
        0x02,

        'a',
        '\n'
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);

    std::cout << "PASS: invalid header value rejected\n";
}


// ============================================================
// RESPONSE TESTS
// ============================================================

static void test_response_encode_decode()
{
    ResponseHeaders response;

    response.status = 200;

    response.headers = {
        {"content-type", "text/plain"},
        {"content-length", "5"}
    };

    auto encoded = encode_response_headers(response);

    assert(!encoded.empty());

    ResponseHeaders decoded;
    std::string error;

    bool ok = decode_response_headers(
        encoded,
        decoded,
        error
    );

    assert(ok);

    assert(decoded.status == 200);
    assert(decoded.headers.size() == 2);

    assert(decoded.headers[0].name == "content-type");
    assert(decoded.headers[0].value == "text/plain");

    assert(decoded.headers[1].name == "content-length");
    assert(decoded.headers[1].value == "5");

    std::cout << "PASS: response encode/decode\n";
}


static void test_truncated_response()
{
    /*
     * Status requires two bytes.
     */
    std::vector<uint8_t> payload = {
        0x00
    };

    ResponseHeaders response;
    std::string error;

    bool ok = decode_response_headers(
        payload,
        response,
        error
    );

    assert(!ok);

    std::cout << "PASS: truncated response rejected\n";
}


static void test_response_trailing_bytes()
{
    ResponseHeaders response;

    response.status = 200;

    response.headers = {
        {"content-type", "text/plain"}
    };

    auto encoded = encode_response_headers(response);

    assert(!encoded.empty());

    encoded.push_back(0xFF);

    ResponseHeaders decoded;
    std::string error;

    bool ok = decode_response_headers(
        encoded,
        decoded,
        error
    );

    assert(!ok);
    assert(error == "trailing bytes in response headers");

    std::cout << "PASS: trailing response bytes rejected\n";
}


static void test_malformed_request_too_short()
{
    std::vector<uint8_t> payload = {
        0x01,
        0x00
    };

    Request request;
    std::string error;

    bool ok = decode_request(
        payload,
        request,
        error
    );

    assert(!ok);
    assert(error == "request too short");

    std::cout << "PASS: too-short request rejected\n";
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    std::cout << "==============================\n";
    std::cout << "Protocol unit tests\n";
    std::cout << "==============================\n";

    // CRC
    test_crc();

    // Request encoding/decoding
    test_request_encode_decode();
    test_empty_path();
    test_path_without_slash();
    test_nul_in_path();
    test_unsupported_method();
    test_truncated_request();
    test_trailing_request_bytes();

    // Headers
    test_malformed_headers();
    test_invalid_header_name();
    test_invalid_header_value();

    // Response encoding/decoding
    test_response_encode_decode();
    test_truncated_response();
    test_response_trailing_bytes();

    // Additional malformed request
    test_malformed_request_too_short();

    // Frame layer
    test_valid_frame();
    test_bad_crc();
    test_bad_magic();
    test_bad_version();
    test_reserved_byte();

    std::cout << "\nAll protocol unit tests passed.\n";

    return 0;
}