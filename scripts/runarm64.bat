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
     cmake --build --preset windows-arm64-msvc-%TYPE%
     set BUILD_DIR=build\windows-arm64-msvc-%TYPE%
) else if "%1" == "build-clang" (
     cmake --build --preset windows-arm64-clang-%TYPE%
     set BUILD_DIR=build\windows-arm64-clang-%TYPE%
) else if "%1" == "configure-msvc" (
     cmake --preset windows-arm64-msvc-%TYPE%
     set BUILD_DIR=build\windows-arm64-msvc-%TYPE%
     exit /b 0
) else if "%1" == "configure-clang" (
     cmake --preset windows-arm64-clang-%TYPE%
     set BUILD_DIR=build\windows-arm64-clang-%TYPE%
     exit /b 0
)
set "error=%errorlevel%"
if NOT %error% == 0 (
     echo Failed: %error%
     exit /b %error%
)

setlocal enabledelayedexpansion

echo [1/4] Copying files...
call "%~dp0copy-files.bat" "%BUILD_DIR%" "BOOTAA64"
if %errorlevel% neq 0 (
     pause
     exit /b 1
)

echo [2/4] Checking for QEMU installation...
where qemu-system-aarch64.exe >nul 2>&1
if %errorlevel% neq 0 (
     echo qemu-system-aarch64 not found
     pause
     exit /b 1
)

echo [3/4] Locating AAVMF firmware...
set "AAVMF_PATH="
set "AAVMF_VARS="

if exist "C:\Program Files\qemu\share\edk2-aarch64-code.fd" (
    set "AAVMF_PATH=C:\Program Files\qemu\share\edk2-aarch64-code.fd"
) else if exist "C:\qemu\share\edk2-aarch64-code.fd" (
    set "AAVMF_PATH=C:\qemu\share\edk2-aarch64-code.fd"
) else if exist "C:\Program Files\qemu\share\AAVMF_CODE.fd" (
    set "AAVMF_PATH=C:\Program Files\qemu\share\AAVMF_CODE.fd"
) else if exist "C:\qemu\share\AAVMF_CODE.fd" (
    set "AAVMF_PATH=C:\qemu\share\AAVMF_CODE.fd"
) else if exist "%~dp0AAVMF_CODE.fd" (
    set "AAVMF_PATH=%~dp0AAVMF_CODE.fd"
)

if "%AAVMF_PATH%"=="" (
     if exist "AAVMF_CODE.fd" (
          set "AAVMF_PATH=AAVMF_CODE.fd"
          echo Found: AAVMF_CODE.fd
     ) else (
          echo No AAVMF found for ARM64.
          pause
          exit /b 1
     )
)

echo AAVMF: !AAVMF_PATH!

echo [4/4] Starting QEMU...
qemu-system-aarch64.exe ^
     -machine virt,gic-version=3 ^
     -cpu cortex-a72 ^
     -drive if=pflash,format=raw,readonly=on,file="!AAVMF_PATH!" ^
     -drive file=fat:rw:%BUILD_DIR%\guest_root,format=raw,media=disk ^
     -m 512M ^
     -net none ^
     -device ramfb ^
     -device qemu-xhci,id=xhci ^
     -device usb-kbd,bus=xhci.0 ^
     -device usb-mouse,bus=xhci.0 ^
     -device virtio-gpu-pci ^
     -serial stdio -d int -D qemu_arm.txt -no-reboot -no-shutdown ^
     -monitor tcp:127.0.0.1:4445,server,nowait

echo.
echo QEMU session ended.
pause
