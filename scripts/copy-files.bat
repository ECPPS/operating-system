@echo off

if "%~1"=="" (
    echo Error: BUILD_DIR parameter required
    exit /b 1
)
if "%~2"=="" (
    echo Error: EFI_IMAGE_NAME parameter required
    exit /b 1
)

net session >nul 2>&1
set "GUEST_ROOT=%BUILD_DIR%\guest_root"
if %errorlevel% == 0 (
    set "GUEST_ROOT=E:"
)
set "BUILD_DIR=%~1"
set "EFI_IMAGE=%~2"

echo [Copying files] Creating directory structure...
if not exist "%GUEST_ROOT%\EFI\BOOT" mkdir "%GUEST_ROOT%\EFI\BOOT"

echo [Copying files] Copying %EFI_IMAGE%.efi -^> %GUEST_ROOT%\EFI\BOOT\%EFI_IMAGE%.EFI...
if not exist "%BUILD_DIR%\bootloader\%EFI_IMAGE%.efi" (
    echo ERROR: %EFI_IMAGE%.efi not found in %BUILD_DIR%
    exit /b 1
)
copy /Y "%BUILD_DIR%\bootloader\%EFI_IMAGE%.efi" "%GUEST_ROOT%\EFI\BOOT\%EFI_IMAGE%.EFI" >nul

if exist "%BUILD_DIR%\kernel\kernel.exe" (
    echo [Copying files] Copying kernel.exe -^> %GUEST_ROOT%\kernel\kernel.exe...
    if not exist "%GUEST_ROOT%\kernel" mkdir "%GUEST_ROOT%\kernel"
    copy /Y "%BUILD_DIR%\kernel\kernel.exe" "%GUEST_ROOT%\kernel\kernel.exe" >nul
)

if exist "%BUILD_DIR%\drivers\BootVideo\BootVideo.dll" (
    echo [Copying files] Copying BootVideo.dll -^> %GUEST_ROOT%\kernel\BootVideo.dll...
    if not exist "%GUEST_ROOT%\kernel" mkdir "%GUEST_ROOT%\kernel"
    copy /Y "%BUILD_DIR%\drivers\BootVideo\BootVideo.dll" "%GUEST_ROOT%\kernel\BootVideo.dll" >nul
)

echo [Copying files] Complete!
exit /b 0
