@echo off
rem 构建 openworld 服务器
rem 前置：已安装 Visual Studio 的「使用 C++ 的桌面开发」工作负载
setlocal

rem 用 vswhere 定位 VS 安装目录（VS 安装器自带，路径固定）
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSDIR=%%i"

if not defined VSDIR (
    echo 未找到 Visual Studio，请先安装「使用 C++ 的桌面开发」工作负载
    exit /b 1
)

rem VS 自带的 CMake 不在 PATH 里，用完整路径调用
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64
"%CMAKE%" --build build --config Release --parallel

endlocal
