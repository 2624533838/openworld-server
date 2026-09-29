// 帧协议：长度前缀 + payload，解决 TCP 粘包/拆包。
// 帧格式：[4 字节大端长度][payload 字节流]

#pragma once

#include <cstdint>
#include <string>

namespace openworld {

// 长度头固定 4 字节（大端 uint32）
inline constexpr std::size_t kFrameHeaderSize = 4;
// 单帧 payload 上限（防御恶意/错误的超大长度）
inline constexpr std::size_t kMaxFrameSize = 1u << 20;  // 1 MB

// 编码：给 payload 加 4 字节长度头，返回完整帧
std::string encode_frame(const std::string& payload);

// 解码器：累积字节流，切出完整帧（处理粘包/半包）。
// 用法：每收到一段数据就 feed，随后反复 pop 直到返回 false。
class FrameDecoder {
public:
    // 喂入新收到的字节
    void feed(const char* data, std::size_t len);

    // 取出一帧的 payload；缓冲里没有完整帧时返回 false。
    // 长度超限等协议错误会抛异常，由上层断开连接。
    bool pop(std::string& payload);

    // 缓冲中尚未处理的字节数（调试用）
    std::size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
};

}  // namespace openworld
