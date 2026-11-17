@echo off
setlocal
set PATH=C:\msys64\mingw32\bin;%PATH%
set SRC=%~dp0
set OUT=E:\re4z\build\bo1z
if not exist "%OUT%" mkdir "%OUT%"

g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra -Wno-unused-parameter ^
    -o "%OUT%\bo1z.dll" "%SRC%bo1z.cpp" -ld3d9 -luser32 -lgdi32 -lpsapi
if errorlevel 1 exit /b 1

g++ -m32 -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra -Wno-unused-parameter ^
    -o "%OUT%\bo1z_inject.exe" "%SRC%inject.cpp"
if errorlevel 1 exit /b 1

g++ -m32 -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    -Wall -Wextra -Wno-unused-parameter ^
    -o "%OUT%\modlist.exe" "%SRC%modlist.cpp" -lpsapi
if errorlevel 1 exit /b 1

echo Built bo1z.dll, bo1z_inject.exe, modlist.exe in %OUT%
