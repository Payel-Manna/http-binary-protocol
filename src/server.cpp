#include "protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace hbin;

namespace {

constexpr size_t DATA_CHUNK = 64 * 1024;


/*
 * Print a short description of a received frame.
 */
void print_frame(
    const char* direction,
    const Frame& f
) {
    std::cerr
        << direction
        << " type="
        << static_cast<int>(f.header.type)
        << " flags=0x"
        << std::hex
        << static_cast<int>(f.header.flags)
        << std::dec
        << " stream="
        << f.header.stream_id
        << " length="
        << f.header.payload_length
        << "\n";
}


/*
 * Send a response consisting of:
 *
 *   RESPONSE_HEADERS
 *   DATA
 *
 * The DATA frame carries END_STREAM.
 */
bool send_simple_response(
    int fd,
    uint16_t stream,
    uint16_t status,
    const std::string& content_type,
    const std::string& body
) {
    ResponseHeaders rh{
        status,
        {
            {"content-type", content_type},
            {"content-length", std::to_string(body.size())}
        }
    };

    auto hp = encode_response_headers(rh);

    if (hp.empty()) {
        return false;
    }

    uint8_t header_flags =
        FLAG_END_HEADERS;

    if (body.empty()) {
        header_flags |= FLAG_END_STREAM;
    }

    if (
        !send_frame(
            fd,
            FrameType::RESPONSE_HEADERS,
            header_flags,
            stream,
            hp
        )
    ) {
        return false;
    }

    if (!body.empty()) {

        std::vector<uint8_t> data(
            body.begin(),
            body.end()
        );

        if (
            !send_frame(
                fd,
                FrameType::DATA,
                FLAG_END_STREAM,
                stream,
                data
            )
        ) {
            return false;
        }
    }

    return true;
}


/*
 * Determine the MIME type from the file extension.
 */
std::string content_type_for(
    const fs::path& path
) {
    const auto ext =
        path.extension().string();

    if (ext == ".html" || ext == ".htm")
        return "text/html";

    if (ext == ".css")
        return "text/css";

    if (ext == ".js")
        return "application/javascript";

    if (ext == ".txt")
        return "text/plain";

    if (ext == ".json")
        return "application/json";

    if (ext == ".png")
        return "image/png";

    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";

    if (ext == ".svg")
        return "image/svg+xml";

    return "application/octet-stream";
}


/*
 * Send a file using one or more DATA frames.
 *
 * RESPONSE_HEADERS contains the complete file size.
 * DATA frames contain the actual bytes.
 */
bool send_file(
    int fd,
    uint16_t stream,
    const fs::path& path,
    uintmax_t size
) {
    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file) {
        return send_simple_response(
            fd,
            stream,
            500,
            "text/plain",
            "cannot open file\n"
        );
    }

    ResponseHeaders rh{
        200,
        {
            {
                "content-type",
                content_type_for(path)
            },
            {
                "content-length",
                std::to_string(size)
            }
        }
    };

    auto hp =
        encode_response_headers(rh);

    if (hp.empty()) {
        return false;
    }

    /*
     * Headers are not the end of the stream
     * because DATA frames follow.
     */
    if (
        !send_frame(
            fd,
            FrameType::RESPONSE_HEADERS,
            FLAG_END_HEADERS,
            stream,
            hp
        )
    ) {
        return false;
    }

    std::vector<uint8_t> buffer(
        DATA_CHUNK
    );

    uintmax_t remaining = size;

    while (remaining > 0) {

        size_t want =
            static_cast<size_t>(
                std::min<uintmax_t>(
                    remaining,
                    buffer.size()
                )
            );

        file.read(
            reinterpret_cast<char*>(
                buffer.data()
            ),
            static_cast<std::streamsize>(
                want
            )
        );

        std::streamsize got =
            file.gcount();

        if (got <= 0) {
            return false;
        }

        std::vector<uint8_t> chunk(
            buffer.begin(),
            buffer.begin() + got
        );

        uint8_t flags =
            (static_cast<uintmax_t>(got) ==
             remaining)
                ? FLAG_END_STREAM
                : FLAG_NONE;

        if (
            !send_frame(
                fd,
                FrameType::DATA,
                flags,
                stream,
                chunk
            )
        ) {
            return false;
        }

        remaining -=
            static_cast<uintmax_t>(got);
    }

    /*
     * Empty files still need a DATA frame
     * carrying END_STREAM.
     */
    if (size == 0) {

        if (
            !send_frame(
                fd,
                FrameType::DATA,
                FLAG_END_STREAM,
                stream,
                {}
            )
        ) {
            return false;
        }
    }

    return true;
}


/*
 * Resolve a URL path safely underneath the
 * configured www root.
 *
 * Examples:
 *
 *   /index.html       -> www/index.html
 *   /hello.txt        -> www/hello.txt
 *
 * Paths containing ".." are rejected.
 * Backslashes are rejected as well.
 */
bool safe_resolve(
    const fs::path& root,
    const std::string& url_path,
    fs::path& result
) {
    /*
     * URL path must not contain backslashes.
     */
    if (
        url_path.find('\\') !=
        std::string::npos
    ) {
        return false;
    }

    /*
     * The protocol requires an absolute-style
     * URL path beginning with '/'.
     */
    if (
        url_path.empty() ||
        url_path[0] != '/'
    ) {
        return false;
    }

    fs::path relative =
        url_path.substr(1);

    /*
     * Reject explicit traversal.
     */
    for (const auto& part : relative) {

        if (part == "..") {
            return false;
        }
    }

    fs::path candidate =
        (root / relative).lexically_normal();

    fs::path root_abs =
        fs::absolute(root).lexically_normal();

    fs::path candidate_abs =
        fs::absolute(candidate).lexically_normal();

    /*
     * Make sure candidate remains underneath root.
     */
    auto root_it = root_abs.begin();
    auto cand_it = candidate_abs.begin();

    for (
        ;
        root_it != root_abs.end() &&
        cand_it != candidate_abs.end();
        ++root_it, ++cand_it
    ) {
        if (*root_it != *cand_it) {
            return false;
        }
    }

    /*
     * Candidate ended before the complete root,
     * therefore it cannot be inside root.
     */
    if (root_it != root_abs.end()) {
        return false;
    }

    result = candidate_abs;

    return true;
}


/*
 * Handle ONE TCP connection.
 *
 * IMPORTANT:
 *
 * accept() happens outside this function.
 *
 * This function can process:
 *
 *   request 1
 *   response 1
 *   request 2
 *   response 2
 *   request 3
 *   response 3
 *
 * on the SAME TCP connection.
 */
void handle_connection(
    int client_fd,
    const fs::path& root
) {
    while (true) {

        Frame frame;

        /*
         * recv_frame() reads the complete frame,
         * including its payload.
         */
        if (!recv_frame(client_fd, frame)) {
            break;
        }

        print_frame("RX", frame);

        FrameType type =
            static_cast<FrameType>(
                frame.header.type
            );


        /*
         * ====================================================
         * UNKNOWN FRAME TYPES
         * ====================================================
         *
         * A receiver that does not understand a frame type
         * MUST skip the complete frame.
         *
         * recv_frame() has already consumed:
         *
         *   header + payload
         *
         * therefore the next frame can be read safely.
         */
        if (
            type != FrameType::REQUEST &&
            type != FrameType::PING &&
            type != FrameType::GOAWAY &&
            type != FrameType::RESPONSE_HEADERS &&
            type != FrameType::DATA
        ) {
            std::cout
                << "Skipping unknown frame type "
                << static_cast<int>(
                    frame.header.type
                )
                << "\n";

            continue;
        }


        /*
         * ====================================================
         * PING
         * ====================================================
         *
         * Echo the payload back.
         */
        if (type == FrameType::PING) {

            if (
                !send_frame(
                    client_fd,
                    FrameType::PING,
                    frame.header.flags,
                    frame.header.stream_id,
                    frame.payload
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * ====================================================
         * GOAWAY
         * ====================================================
         *
         * Client explicitly asks to end the connection.
         */
        if (type == FrameType::GOAWAY) {
            break;
        }


        /*
         * ====================================================
         * CLIENT MUST NOT SEND RESPONSE FRAMES
         * ====================================================
         *
         * RESPONSE_HEADERS and DATA are server-to-client
         * frame types.
         */
        if (
            type == FrameType::RESPONSE_HEADERS ||
            type == FrameType::DATA
        ) {
            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    400,
                    "text/plain",
                    "client sent response frame\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * ====================================================
         * REQUEST
         * ====================================================
         */

        /*
         * Stream ID zero is reserved.
         */
        if (
            frame.header.stream_id == 0
        ) {
            if (
                !send_simple_response(
                    client_fd,
                    0,
                    400,
                    "text/plain",
                    "stream id must be non-zero\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * The request frame itself must terminate
         * the request stream.
         */
        if (
            (frame.header.flags &
             FLAG_END_STREAM) == 0
        ) {
            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    400,
                    "text/plain",
                    "request must end stream\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * Decode binary request payload.
         */
        Request req{};
        std::string error;

        if (
            !decode_request(
                frame.payload,
                req,
                error
            )
        ) {
            std::cerr
                << "bad request: "
                << error
                << "\n";

            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    400,
                    "text/plain",
                    "bad request: " +
                    error +
                    "\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * At the moment the protocol supports GET.
         */
        if (req.method != Method::GET) {

            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    400,
                    "text/plain",
                    "unsupported method\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * Safely map URL path to www root.
         */
        fs::path file;

        if (
            !safe_resolve(
                root,
                req.path,
                file
            )
        ) {
            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    400,
                    "text/plain",
                    "invalid path\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * Check whether the requested path exists
         * and is a regular file.
         */
        std::error_code ec;

        bool exists =
            fs::exists(file, ec);

        bool regular =
            exists &&
            fs::is_regular_file(
                file,
                ec
            );

        if (!exists || !regular) {

            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    404,
                    "text/plain",
                    "not found\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * Obtain file size.
         */
        uintmax_t size =
            fs::file_size(
                file,
                ec
            );

        if (ec) {

            if (
                !send_simple_response(
                    client_fd,
                    frame.header.stream_id,
                    500,
                    "text/plain",
                    "cannot stat file\n"
                )
            ) {
                break;
            }

            continue;
        }


        /*
         * Send:
         *
         *   RESPONSE_HEADERS
         *   DATA...
         *
         * Do NOT close the TCP connection afterward.
         *
         * The outer while loop waits for another
         * request on the same connection.
         */
        if (
            !send_file(
                client_fd,
                frame.header.stream_id,
                file,
                size
            )
        ) {
            break;
        }
    }
}

} // namespace


int main(
    int argc,
    char** argv
) {
    if (argc != 3) {

        std::cerr
            << "usage: "
            << argv[0]
            << " <www-root> <port>\n";

        return 2;
    }


    /*
     * Resolve www root.
     */
    fs::path root =
        fs::absolute(
            argv[1]
        ).lexically_normal();

    if (
        !fs::exists(root) ||
        !fs::is_directory(root)
    ) {
        std::cerr
            << "root is not a directory: "
            << root
            << "\n";

        return 2;
    }


    /*
     * Parse port.
     */
    int port = 0;

    try {
        port = std::stoi(argv[2]);
    }
    catch (...) {
        std::cerr
            << "invalid port\n";

        return 2;
    }

    if (
        port < 1 ||
        port > 65535
    ) {
        std::cerr
            << "port must be 1..65535\n";

        return 2;
    }


    /*
     * Prevent SIGPIPE from terminating the server
     * when a client disconnects unexpectedly.
     */
    std::signal(
        SIGPIPE,
        SIG_IGN
    );


    /*
     * Create TCP socket.
     */
    int server_fd =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }


    /*
     * Allow immediate restart after shutdown.
     */
    int yes = 1;

    ::setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &yes,
        sizeof(yes)
    );


    /*
     * Bind to all IPv4 interfaces.
     */
    sockaddr_in addr{};

    addr.sin_family =
        AF_INET;

    addr.sin_addr.s_addr =
        htonl(INADDR_ANY);

    addr.sin_port =
        htons(
            static_cast<uint16_t>(
                port
            )
        );


    if (
        ::bind(
            server_fd,
            reinterpret_cast<sockaddr*>(
                &addr
            ),
            sizeof(addr)
        ) < 0
    ) {
        perror("bind");

        ::close(server_fd);

        return 1;
    }


    /*
     * Listen for TCP connections.
     */
    if (
        ::listen(
            server_fd,
            16
        ) < 0
    ) {
        perror("listen");

        ::close(server_fd);

        return 1;
    }


    std::cout
        << "httpbin server listening "
           "on port "
        << port
        << " with root "
        << root
        << "\n";


    /*
     * Accept loop.
     *
     * Each accepted socket is handed to
     * handle_connection().
     *
     * handle_connection() itself keeps the
     * socket alive for multiple requests.
     */
    while (true) {

        sockaddr_in client_addr{};

        socklen_t len =
            sizeof(client_addr);

        int client_fd =
            ::accept(
                server_fd,
                reinterpret_cast<sockaddr*>(
                    &client_addr
                ),
                &len
            );

        if (client_fd < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("accept");

            break;
        }


        std::cout
            << "accepted TCP connection\n";


        /*
         * IMPORTANT:
         *
         * This function may process many
         * requests before returning.
         */
        handle_connection(
            client_fd,
            root
        );


        std::cout
            << "connection closed\n";


        ::close(client_fd);
    }


    ::close(server_fd);

    return 0;
}