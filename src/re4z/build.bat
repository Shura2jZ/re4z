@echo off
setlocal
set PATH=E:\msys64\mingw32\bin;%PATH%
set SRC=%~dp0
set OUT=E:\re4z\build\re4z
set BIN=E:\re4z\game\Bin32
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%BIN%" mkdir "%BIN%"

g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra -Wno-unused-parameter ^
    -o "%BIN%\RE4Z.asi" ^
    "%SRC%dllmain.cpp" "%SRC%log.cpp" "%SRC%game.cpp" "%SRC%camera.cpp" ^
    "%SRC%zombies.cpp" "%SRC%viewmodel.cpp" "%SRC%bo1guest.cpp" "%SRC%overlay.cpp" "%SRC%d3d9hook.cpp" ^
    "%SRC%shooting.cpp" "%SRC%hud_assets.cpp" ^
    -ld3d9 -luser32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Built %BIN%\RE4Z.asi
