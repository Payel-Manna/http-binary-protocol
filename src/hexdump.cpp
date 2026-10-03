#include "protocol.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace hbin;

static void dump(const std::string& title, const std::vector<uint8_t>& b) {
    std::cout << title << " (" << b.size() << " bytes)\n";
    for (size_t i = 0; i < b.size(); i += 16) {
        std::cout << std::hex << std::setfill('0') << std::setw(8) << i << "  ";
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < b.size()) std::cout << std::setw(2) << unsigned(b[i+j]) << ' ';
            else std::cout << "   ";
        }
        std::cout << " |";
        for (size_t j = 0; j < 16 && i+j < b.size(); ++j) {
            unsigned char c = b[i+j];
            std::cout << ((c >= 32 && c <= 126) ? char(c) : '.');
        }
        std::cout << "|\n";
    }
    std::cout << std::dec;
}

static std::vector<uint8_t> frame_bytes(FrameType type, uint8_t flags,
                                         uint16_t stream, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b(16 + payload.size());
    uint16_t magic = PROTOCOL_MAGIC;
    uint32_t len = payload.size();
    uint32_t crc = crc32(payload);
    b[0]=magic>>8; b[1]=magic;
    b[2]=PROTOCOL_VERSION; b[3]=static_cast<uint8_t>(type); b[4]=flags; b[5]=0;
    b[6]=stream>>8; b[7]=stream;
    b[8]=len>>24; b[9]=len>>16; b[10]=len>>8; b[11]=len;
    b[12]=crc>>24; b[13]=crc>>16; b[14]=crc>>8; b[15]=crc;
    std::copy(payload.begin(), payload.end(), b.begin()+16);
    return b;
}

int main() {
    Request req{Method::GET, "/index.html", {
        {"host", "localhost"},
        {"accept", "*/*"}
    }};
    auto rp = encode_request(req);
    dump("REQUEST", frame_bytes(FrameType::REQUEST, FLAG_END_STREAM, 1, rp));

    ResponseHeaders rh{200, {
        {"content-type", "text/html"},
        {"content-length", "13"}
    }};
    auto hp = encode_response_headers(rh);
    dump("RESPONSE HEADERS", frame_bytes(FrameType::RESPONSE_HEADERS,
                                         FLAG_END_HEADERS, 1, hp));

    std::vector<uint8_t> body{'H','e','l','l','o',' ','b','i','n','a','r','y','!'};
    dump("RESPONSE DATA", frame_bytes(FrameType::DATA, FLAG_END_STREAM, 1, body));
}
