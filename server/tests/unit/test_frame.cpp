// 帧协议单测：长度前缀编解码、粘包/半包、长度上限防御。
// 覆盖 spec「粘包/拆包」的纯逻辑部分（Reactor 异步部分由 Python 集成测试覆盖）。

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "frame.h"

namespace openworld {
namespace {

std::uint32_t read_header_len(const std::string& frame) {
    const auto* p = reinterpret_cast<const unsigned char*>(frame.data());
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           (static_cast<std::uint32_t>(p[3]));
}

TEST(FrameTest, EncodeRoundTrip) {
    const std::string payload = "hello, protobuf frame";
    const std::string frame = encode_frame(payload);
    ASSERT_EQ(frame.size(), kFrameHeaderSize + payload.size());
    EXPECT_EQ(read_header_len(frame), payload.size());
    EXPECT_EQ(frame.substr(kFrameHeaderSize), payload);
}

TEST(FrameTest, EncodeEmptyPayload) {
    const std::string frame = encode_frame("");
    ASSERT_EQ(frame.size(), kFrameHeaderSize);
    EXPECT_EQ(read_header_len(frame), 0u);
}

TEST(FrameTest, EncodeOversizeThrows) {
    EXPECT_THROW(encode_frame(std::string(kMaxFrameSize + 1, 'x')), std::runtime_error);
}

TEST(FrameTest, DecodeHalfPacket) {
    FrameDecoder dec;
    const std::string payload = "abc";
    const std::string frame = encode_frame(payload);
    std::string out;

    dec.feed(frame.data(), kFrameHeaderSize - 1);  // 长度头都不全
    EXPECT_FALSE(dec.pop(out));
    dec.feed(frame.data() + kFrameHeaderSize - 1, 1);  // 补齐长度头
    EXPECT_FALSE(dec.pop(out));                          // payload 还没到
    dec.feed(frame.data() + kFrameHeaderSize, payload.size());  // 补齐 payload
    ASSERT_TRUE(dec.pop(out));
    EXPECT_EQ(out, payload);
    EXPECT_EQ(dec.buffered(), 0u);
}

TEST(FrameTest, DecodeStickyPacket) {
    FrameDecoder dec;
    const std::string f1 = encode_frame("first");
    const std::string f2 = encode_frame("second");
    dec.feed((f1 + f2).data(), f1.size() + f2.size());  // 一次喂两帧
    std::string out;
    ASSERT_TRUE(dec.pop(out));
    EXPECT_EQ(out, "first");
    ASSERT_TRUE(dec.pop(out));
    EXPECT_EQ(out, "second");
    EXPECT_FALSE(dec.pop(out));  // 没有了
}

TEST(FrameTest, DecodeOversizeLengthThrows) {
    FrameDecoder dec;
    // 伪造长度头声称 2MB（超 kMaxFrameSize=1MB）
    const char header[4] = {0x00, 0x20, 0x00, 0x00};  // 0x00200000 = 2MB
    dec.feed(header, 4);
    std::string out;
    EXPECT_THROW(dec.pop(out), std::runtime_error);
}

}  // namespace
}  // namespace openworld
