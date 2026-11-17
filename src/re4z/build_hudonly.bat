@echo off
setlocal
set PATH=E:\msys64\mingw32\bin;%PATH%
set SRC=%~dp0
set OUT=E:\re4z\build\bo1z
set BIN=E:\re4z\game\Bin32
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%BIN%" mkdir "%BIN%"

g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra -Wno-unused-parameter ^
    -o "%BIN%\BO1Z_new.asi" ^
    "%SRC%dllmain_hudonly.cpp" "%SRC%log.cpp" "%SRC%game.cpp" "%SRC%zombies.cpp" "%SRC%overlay.cpp" "%SRC%hud_assets.cpp" "%SRC%d3d9hook.cpp" "%SRC%bo1_catalog.cpp" "%SRC%mapredirect.cpp" ^
    -ld3d9 -luser32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Built %BIN%\BO1Z_new.asi