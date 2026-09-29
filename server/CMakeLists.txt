cmake_minimum_required(VERSION 3.20)
project(openworld-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 依赖：standalone Asio（header-only，Windows IOCP / Linux epoll）----
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
)
FetchContent_MakeAvailable(asio)

# ---- echo 服务器（第 1 周里程碑）----
add_executable(echo_server src/main.cpp)
target_link_libraries(echo_server PRIVATE asio::asio)
target_compile_definitions(echo_server PRIVATE ASIO_STANDALONE)

# Windows 需要 Winsock
if(WIN32)
    target_link_libraries(echo_server PRIVATE ws2_32 mswsock)
endif()
