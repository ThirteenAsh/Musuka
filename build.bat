@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found in PATH.
    echo Install CMake and Visual Studio Build Tools, or open this folder with Visual Studio.
    exit /b 1
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo Visual Studio 2022 generator failed. Retrying with CMake default generator...
    cmake -S . -B build
    if errorlevel 1 exit /b 1
)

cmake --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Build complete: build\Release\musuka.exe
