@echo off
setlocal
set IW4L_GAMES=F:\Games\pluto_t5_full_game
set IW4L_ARTIFACTS_DIR=E:\re4z\build\iw4l-artifacts
set IW4L_BENCH=0

echo === RE4Z Composite Test ===
echo 1. Lancement du runtime IW4L en mode bridge (viewmodel BO1 offscreen)...
start "iw4l-bridge" "F:\Desktop\Divers\Deep\MW2-RE4-main\iw4L-master\target\x86_64-pc-windows-gnu\debug\iw4l.exe" bridge t5:mp_array

echo 2. Attente 30s pour le chargement...
timeout /t 30 /nobreak >nul

echo 3. Verifier que le shmem a des pixels non-nuls :
powershell -Command "$h=[System.Runtime.InteropServices.Marshal]::ReadInt32([System.Runtime.InteropServices.Marshal]::ReadIntPtr((Add-Type -MemberDefinition '[DllImport(\"kernel32.dll\")] public static extern IntPtr OpenFileMapping(uint,uint,string);' -Name K -PassThru)::OpenFileMapping(4,$false,'Local\\RE4Z_BO1'),4); Write-Host 'seq=' $h"

echo.
echo 4. Lancer RE4 (Bin32\bio4.exe) manuellement avec RE4Z.asi dans Bin32.
echo    Le viewmodel BO1 devrait apparaitre sur le monde RE4.
echo.
pause