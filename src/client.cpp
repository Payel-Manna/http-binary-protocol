#include "protocol.hpp"

#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

using namespace hbin;

static void print_hex(
    const std::vector<uint8_t>& data
) {
    for (size_t i = 0; i < data.size(); ++i) {

        if (i % 16 == 0) {
            std::printf("%08zx  ", i);
        }

        std::printf("%02x ", data[i]);

        if (i % 16 == 15 || i + 1 == data.size()) {
            std::printf("\n");
        }
    }
}

static bool parse_url(
    const std::string& url,
    std::string& host,
    int& port,
    std::string& path
) {
    std::string input = url;

    const std::string prefix = "http://";

    if (input.rfind(prefix, 0) == 0) {
        input = input.substr(prefix.size());
    }

    size_t slash = input.find('/');

    std::string authority;

    if (slash == std::string::npos) {
        authority = input;
        path = "/";
    } else {
        authority = input.substr(0, slash);
        path = input.substr(slash);

        if (path.empty()) {
            path = "/";
        }
    }

    size_t colon = authority.find(':');

    if (colon == std::string::npos) {
        host = authority;
        port = 9000;
    } else {
        host = authority.substr(0, colon);

        try {
            port = std::stoi(
                authority.substr(colon + 1)
            );
        } catch (...) {
            return false;
        }
    }

    return !host.empty() &&
           port > 0 &&
           port <= 65535;
}

static int connect_to_server(
    const std::string& host,
    int port
) {
    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (fd < 0) {
        perror("socket");
        return -1;
    }

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_port =
        htons(static_cast<uint16_t>(port));

    if (
        inet_pton(
            AF_INET,
            host.c_str(),
            &addr.sin_addr
        ) != 1
    ) {
        std::cerr
            << "Invalid IPv4 address: "
            << host << "\n";

        close(fd);
        return -1;
    }

    if (
        connect(
            fd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)
        ) < 0
    ) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

static void print_frame_hex(
    const std::string& title,
    const Frame& frame
) {
    std::vector<uint8_t> raw;

    const auto& h = frame.header;

    raw.push_back(
        static_cast<uint8_t>((h.magic >> 8) & 0xff)
    );

    raw.push_back(
        static_cast<uint8_t>(h.magic & 0xff)
    );

    raw.push_back(h.version);
    raw.push_back(h.type);
    raw.push_back(h.flags);
    raw.push_back(h.reserved);

    raw.push_back(
        static_cast<uint8_t>(
            (h.stream_id >> 8) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            h.stream_id & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_length >> 24) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_length >> 16) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_length >> 8) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            h.payload_length & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_crc32 >> 24) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_crc32 >> 16) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            (h.payload_crc32 >> 8) & 0xff
        )
    );

    raw.push_back(
        static_cast<uint8_t>(
            h.payload_crc32 & 0xff
        )
    );

    raw.insert(
        raw.end(),
        frame.payload.begin(),
        frame.payload.end()
    );

    std::cout << title
              << " ("
              << raw.size()
              << " bytes)\n";

    print_hex(raw);

    std::cout << "\n";
}

static int request_one(
    int fd,
    const std::string& path,
    uint16_t stream_id,
    bool verbose
) {
    Request request;

    request.method = Method::GET;
    request.path = path;

    request.headers = {
        {"host", "localhost"},
        {"accept", "*/*"}
    };

    std::vector<uint8_t> payload =
        encode_request(request);

    if (payload.empty()) {
        std::cerr
            << "Could not encode request\n";

        return 1;
    }

    if (
        !send_frame(
            fd,
            FrameType::REQUEST,
            FLAG_END_STREAM,
            stream_id,
            payload
        )
    ) {
        std::cerr
            << "Failed to send request\n";

        return 1;
    }

    if (verbose) {
        Frame sent;

        sent.header.magic = PROTOCOL_MAGIC;
        sent.header.version = PROTOCOL_VERSION;
        sent.header.type =
            static_cast<uint8_t>(
                FrameType::REQUEST
            );
        sent.header.flags = FLAG_END_STREAM;
        sent.header.reserved = 0;
        sent.header.stream_id = stream_id;
        sent.header.payload_length =
            static_cast<uint32_t>(
                payload.size()
            );
        sent.header.payload_crc32 =
            crc32(payload);

        sent.payload = payload;

        print_frame_hex(
            "REQUEST FRAME",
            sent
        );
    }

    bool got_headers = false;
    bool got_data = false;

    uint16_t status = 0;

    while (true) {

        Frame frame;

        if (!recv_frame(fd, frame)) {
            std::cerr
                << "Connection closed or invalid frame\n";

            return 1;
        }

        if (
            frame.header.stream_id !=
            stream_id
        ) {
            std::cerr
                << "Unexpected stream ID\n";

            return 1;
        }

        FrameType type =
            static_cast<FrameType>(
                frame.header.type
            );

        if (type == FrameType::RESPONSE_HEADERS) {

            ResponseHeaders response;
            std::string error;

            if (
                !decode_response_headers(
                    frame.payload,
                    response,
                    error
                )
            ) {
                std::cerr
                    << "Invalid response headers: "
                    << error
                    << "\n";

                return 1;
            }

            status = response.status;
            got_headers = true;

            std::cout
                << "HTTP status: "
                << status
                << "\n";

            for (const auto& header :
                 response.headers) {

                std::cout
                    << header.name
                    << ": "
                    << header.value
                    << "\n";
            }

            std::cout << "\n";

            if (verbose) {
                print_frame_hex(
                    "RESPONSE HEADERS",
                    frame
                );
            }

            if (
                frame.header.flags &
                FLAG_END_STREAM
            ) {
                break;
            }

            continue;
        }

        if (type == FrameType::DATA) {

            got_data = true;

            if (verbose) {
                print_frame_hex(
                    "RESPONSE DATA",
                    frame
                );
            }

            std::cout.write(
                reinterpret_cast<
                    const char*
                >(frame.payload.data()),
                static_cast<std::streamsize>(
                    frame.payload.size()
                )
            );

            std::cout.flush();

            if (
                frame.header.flags &
                FLAG_END_STREAM
            ) {
                std::cout << "\n";
                break;
            }

            continue;
        }

        /*
         * Unknown response frame:
         *
         * recv_frame() already consumed
         * its complete payload.
         *
         * Therefore it is safe to skip.
         */
        continue;
    }

    if (!got_headers) {
        std::cerr
            << "No response headers received\n";

        return 1;
    }

    if (!got_data && status >= 200 && status < 300) {
        std::cerr
            << "No response data received\n";

        return 1;
    }

    if (status >= 400) {
        return 2;
    }

    return 0;
}

int main(
    int argc,
    char* argv[]
) {
    bool verbose = false;

    std::vector<std::string> urls;

    for (int i = 1; i < argc; ++i) {

        std::string arg = argv[i];

        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
            continue;
        }

        urls.push_back(arg);
    }

    if (urls.empty()) {

        std::cerr
            << "Usage:\n"
            << "  "
            << argv[0]
            << " [-v] URL [URL ...]\n\n"
            << "Examples:\n"
            << "  "
            << argv[0]
            << " localhost:9000/index.html\n"
            << "  "
            << argv[0]
            << " -v "
            << "localhost:9000/index.html "
            << "localhost:9000/hello.txt\n";

        return 1;
    }

    std::string host;
    int port;
    std::string first_path;

    if (
        !parse_url(
            urls[0],
            host,
            port,
            first_path
        )
    ) {
        std::cerr
            << "Invalid URL: "
            << urls[0]
            << "\n";

        return 1;
    }

    int fd =
        connect_to_server(
            host,
            port
        );

    if (fd < 0)
        return 1;

    std::cout
        << "Connected to "
        << host
        << ":"
        << port
        << "\n";

    int exit_code = 0;

    uint16_t stream_id = 1;

    for (const auto& url : urls) {

        std::string current_host;
        int current_port;
        std::string path;

        if (
            !parse_url(
                url,
                current_host,
                current_port,
                path
            )
        ) {
            std::cerr
                << "Invalid URL: "
                << url
                << "\n";

            exit_code = 1;
            break;
        }

        if (
            current_host != host ||
            current_port != port
        ) {
            std::cerr
                << "All URLs must use the same "
                   "host and port for a "
                   "persistent connection.\n";

            exit_code = 1;
            break;
        }

        std::cout
            << "\n===== REQUEST "
            << stream_id
            << ": "
            << path
            << " =====\n";

        int result =
            request_one(
                fd,
                path,
                stream_id,
                verbose
            );

        if (result != 0) {
            exit_code = result;
        }

        ++stream_id;

        /*
         * We deliberately DO NOT close()
         * the socket here.
         *
         * The same TCP connection is reused
         * for the next request.
         */
    }

    /*
     * One and only one TCP connection was
     * created for the entire client session.
     */
    close(fd);

    std::cout
        << "\nTCP connection closed.\n";

    return exit_code;
}