@echo off
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul
set PATH=C:\Users\azk\hardware-monitor\uefi\nasm-3.02;%PATH%
set NASM_PREFIX=C:\Users\azk\hardware-monitor\uefi\nasm-3.02\
cd /d C:\Users\azk\hardware-monitor\uefi\edk2
call C:\Users\azk\hardware-monitor\uefi\edk2\edksetup.bat >nul
call C:\Users\azk\hardware-monitor\uefi\edk2\BaseTools\BinWrappers\WindowsLike\build.bat -p MdeModulePkg\MdeModulePkg.dsc -m MdeModulePkg\Application\HelloWorld\HelloWorld.inf -a X64 -b RELEASE -t VS2022
