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

# 生成 protobuf C++ 代码（构建时自动生成，proto 是唯一真源）
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
protobuf_generate(
    LANGUAGE cpp
    IMPORT_DIRS "${PROTO_DIR}"
    PROTOS "${PROTO_DIR}/openworld.proto"
    OUT_VAR PROTO_GENERATED
)
# 生成的 pb 提成静态库，服务器与单测共用（避免重复生成/编译）
add_library(openworld_proto STATIC ${PROTO_GENERATED})
target_link_libraries(openworld_proto PUBLIC protobuf::libprotobuf)
target_include_directories(openworld_proto PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

# ---- 服务器可执行 ----
add_executable(openworld_server
    src/main.cpp
    src/connection.cpp
    src/frame.cpp
    src/player.cpp
    src/world.cpp
    src/redis_client.cpp
    src/persistence.cpp
)

target_link_libraries(openworld_server PRIVATE
    asio
    openworld_proto
)

# 生成的 openworld.pb.h 在构建目录（openworld_proto 已 PUBLIC 引入，这里保留显式声明兜底）
target_include_directories(openworld_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Windows 需要 Winsock，并指定 Windows API 版本（消掉 _WIN32_WINNT 警告）
if(WIN32)
    target_link_libraries(openworld_server PRIVATE ws2_32 mswsock)
    target_compile_definitions(openworld_server PRIVATE _WIN32_WINNT=0x0601)
endif()

# MySQL C API（libmysqlclient）—— 冷存档。装在默认路径则自动启用。
set(MYSQL_DIR "C:/Program Files/MySQL/MySQL Server 8.0" CACHE PATH "MySQL 安装根目录")
if(EXISTS "${MYSQL_DIR}/include/mysql.h")
    target_include_directories(openworld_server PRIVATE "${MYSQL_DIR}/include")
    target_link_libraries(openworld_server PRIVATE "${MYSQL_DIR}/lib/libmysql.lib")
    # 运行时依赖：把 libmysql.dll 拷到 exe 旁
    add_custom_command(TARGET openworld_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MYSQL_DIR}/lib/libmysql.dll"
            "$<TARGET_FILE_DIR:openworld_server>")
    message(STATUS "MySQL 持久化：已启用（${MYSQL_DIR}）")
else()
    message(WARNING "未找到 ${MYSQL_DIR}/include/mysql.h，持久化编译将失败")
endif()

# ---- 单元测试（GoogleTest，vendor 源码见 third_party/googletest）----
enable_testing()
# 只编 gtest/gtest_main（不编 gmock）；GOOGLETEST_VERSION 等变量由顶层 CMakeLists 提供，
# 因此必须 add_subdirectory 顶层目录而非 googletest 子目录。
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest)

add_executable(openworld_tests
    tests/unit/test_frame.cpp
    tests/unit/test_grid.cpp
    tests/unit/test_player.cpp
    src/frame.cpp
    src/player.cpp
    src/connection.cpp
)

target_link_libraries(openworld_tests PRIVATE
    gtest_main
    asio
    openworld_proto
)

target_include_directories(openworld_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(WIN32)
    target_link_libraries(openworld_tests PRIVATE ws2_32 mswsock)
    target_compile_definitions(openworld_tests PRIVATE _WIN32_WINNT=0x0601)
endif()

add_test(NAME openworld_tests COMMAND openworld_tests)
