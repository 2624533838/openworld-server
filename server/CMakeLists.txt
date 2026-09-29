cmake_minimum_required(VERSION 3.20)
project(openworld-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC：源文件是 UTF-8 编码，中文 Windows 默认按 GBK 读会乱码，必须加 /utf-8
if(MSVC)
    add_compile_options(/utf-8)
    # protobuf 静态库按 /MT（静态 CRT）编译，这里保持一致，避免链接期 RuntimeLibrary 不匹配
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- 依赖 ----
# standalone Asio（header-only，已 vendor，离线构建）
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio")
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

# protobuf（已 vendor 编译好的静态库 + protoc，见 third_party/protobuf/）
find_package(protobuf CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf"
    NO_DEFAULT_PATH)

# ---- 服务器可执行 ----
add_executable(openworld_server
    src/main.cpp
    src/connection.cpp
    src/frame.cpp
)

# 生成 protobuf C++ 代码（构建时自动生成，proto 是唯一真源）
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
protobuf_generate(
    LANGUAGE cpp
    TARGET openworld_server
    IMPORT_DIRS "${PROTO_DIR}"
    PROTOS "${PROTO_DIR}/openworld.proto"
)

target_link_libraries(openworld_server PRIVATE
    asio
    protobuf::libprotobuf
)

# 生成的 openworld.pb.h 在构建目录，需要加进 include 路径
target_include_directories(openworld_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Windows 需要 Winsock，并指定 Windows API 版本（消掉 _WIN32_WINNT 警告）
if(WIN32)
    target_link_libraries(openworld_server PRIVATE ws2_32 mswsock)
    target_compile_definitions(openworld_server PRIVATE _WIN32_WINNT=0x0601)
endif()
