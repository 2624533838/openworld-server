@echo off
rem 构建 echo 服务器（第 1 周里程碑）
rem 前置：已安装 Visual Studio 2022 的「使用 C++ 的桌面开发」工作负载
cmake -S . -B build
cmake --build build --config Release
