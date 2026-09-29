#include "frame.h"

#include <stdexcept>

namespace openworld {

std::string encode_frame(const std::string& payload) {
    if (payload.size() > kMaxFrameSize) {
        throw std::runtime_error("encode_frame: payload exceeds kMaxFrameSize");
    }
    const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
    frame.append(payload);
    return frame;
}

void FrameDecoder::feed(const char* data, std::size_t len) {
    buf_.append(data, len);
}

bool FrameDecoder::pop(std::string& payload) {
    if (buf_.size() < kFrameHeaderSize) {
        return false;  // 半包：长度头还没收全
    }
    const auto* p = reinterpret_cast<const unsigned char*>(buf_.data());
    const std::uint32_t len =
        (static_cast<std::uint32_t>(p[0]) << 24) |
        (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) |
        (static_cast<std::uint32_t>(p[3]));
    if (len > kMaxFrameSize) {
        throw std::runtime_error("FrameDecoder: frame length exceeds kMaxFrameSize");
    }
    if (buf_.size() < kFrameHeaderSize + len) {
        return false;  // 半包：payload 还没收全
    }
    payload.assign(buf_, kFrameHeaderSize, len);
    buf_.erase(0, kFrameHeaderSize + len);
    return true;
}

}  // namespace openworld
