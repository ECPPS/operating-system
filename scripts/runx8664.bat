@echo off

set "TYPE="
set "BUILD_DIR="

if "%2" == "debug" (
     set "TYPE=debug"
) else if "%2" == "release" (
     set "TYPE=release"
) else (
     echo Invalid preset
     exit /b 1
)

if "%1" == "build-msvc" (
     cmake --build --preset windows-x64-msvc-%TYPE%
     set BUILD_DIR=build\windows-x64-msvc-%TYPE%
) else if "%1" == "build-clang" (
     cmake --build --preset windows-x64-clang-%TYPE%
     set BUILD_DIR=build\windows-x64-clang-%TYPE%
) else if "%1" == "configure-msvc" (
     cmake --preset windows-x64-msvc-%TYPE%
     set BUILD_DIR=build\windows-x64-msvc-%TYPE%
     exit /b 0
) else if "%1" == "configure-clang" (
     cmake --preset windows-x64-clang-%TYPE%
     set BUILD_DIR=build\windows-x64-clang-%TYPE%
     exit /b 0
)
set "error=%errorlevel%"
if NOT %error% == 0 (
     echo Failed: %error%
     exit /b %error%
)

setlocal enabledelayedexpansion

echo [1/4] Copying files...
call "%~dp0copy-files.bat" "%BUILD_DIR%" "BOOTX64"
if %errorlevel% neq 0 (
     pause
     exit /b 1
)

echo [2/4] Checking for QEMU installation...
where qemu-system-x86_64.exe >nul 2>&1
if %errorlevel% neq 0 (
     echo qemu-system-x86_64 not found
     pause
     exit /b 1
)

echo [3/4] Locating OVMF firmware...
set "OVMF_PATH="

if exist "C:\Program Files\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_PATH=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
) else if exist "C:\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_PATH=C:\qemu\share\edk2-x86_64-code.fd"
) else if exist "C:\Program Files\qemu\share\OVMF_CODE.fd" (
    set "OVMF_PATH=C:\Program Files\qemu\share\OVMF_CODE.fd"
) else if exist "C:\qemu\share\OVMF_CODE.fd" (
    set "OVMF_PATH=C:\qemu\share\OVMF_CODE.fd"
) else if exist "%~dp0OVMF_CODE.fd" (
    set "OVMF_PATH=%~dp0OVMF_CODE.fd"
) else if exist "C:\msys64\ucrt64\share\qemu\edk2-x86_64-code.fd" (
    set "OVMF_PATH=C:\msys64\ucrt64\share\qemu\edk2-x86_64-code.fd"
) 

if "%OVMF_PATH%"=="" (
     if exist "OVMF_CODE.fd" (
          set "OVMF_PATH=OVMF_CODE.fd"
          echo Found: OVMF_CODE.fd
     ) else (
          echo No OVMF found.
          pause
          exit /b 1
     )
)

echo OVMF: !OVMF_PATH!

echo [4/4] Starting QEMU...
qemu-system-x86_64.exe ^
 -M q35 ^
 -cpu max,+vmx,+avx,+avx2,+xsave,+xsaveopt ^
 -m 512M ^
 -drive if=pflash,format=raw,readonly=on,file="!OVMF_PATH!" ^
 -drive file=fat:rw:%BUILD_DIR%\guest_root,format=raw,media=disk ^
 -net none ^
 -vga std ^
 -serial stdio ^
 -no-reboot -no-shutdown

echo.
echo QEMU session ended.
pause
