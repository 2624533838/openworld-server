cmake_minimum_required(VERSION 3.20)
project(openworld-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC：源文件是 UTF-8 编码，中文 Windows 默认按 GBK 读会乱码，必须加 /utf-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- 依赖：standalone Asio（header-only）----
# 已 vendor 到 third_party/asio，离线构建，不依赖网络/GitHub。
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio")
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

# ---- echo 服务器（第 1 周里程碑）----
add_executable(echo_server src/main.cpp)
target_link_libraries(echo_server PRIVATE asio)

# Windows 需要 Winsock，并指定 Windows API 版本（消掉 _WIN32_WINNT 警告）
if(WIN32)
    target_link_libraries(echo_server PRIVATE ws2_32 mswsock)
    target_compile_definitions(echo_server PRIVATE _WIN32_WINNT=0x0601)
endif()
