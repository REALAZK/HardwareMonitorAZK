@echo off
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul
if errorlevel 1 (
    echo vcvars failed
    exit /b 1
)
set PATH=C:\Users\azk\hardware-monitor\uefi\nasm-3.02;%PATH%
set NASM_PREFIX=C:\Users\azk\hardware-monitor\uefi\nasm-3.02\
cd /d C:\Users\azk\hardware-monitor\uefi\edk2
echo CWD is %CD%
echo PATHEXT is %PATHEXT%
call "C:\Users\azk\hardware-monitor\uefi\edk2\edksetup.bat" Rebuild
