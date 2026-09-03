@echo off
setlocal

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul
if errorlevel 1 (
    echo Failed to initialize MSVC environment.
    exit /b 1
)

set WDK=C:\Program Files (x86)\Windows Kits\10
set WDKVER=10.0.26100.0
set ROOT=%~dp0
set OUT=%ROOT%build\

if not exist "%OUT%" mkdir "%OUT%"

echo Assembling hwmon_hv_asm.asm...
ml64 /nologo /c /Fo"%OUT%hwmon_hv_asm.obj" "%ROOT%hwmon_hv_asm.asm"
if errorlevel 1 goto :fail

echo Compiling hwmon_hv.c (kernel mode)...
cl.exe /c /nologo /W3 /WX- /Od /Zi ^
    /D_AMD64_ /D_WIN64 /D_M_AMD64 /DDBG=0 ^
    /Gm- /EHs-c- /GR- /GF /Zp8 /GS /kernel ^
    /I "%WDK%\Include\%WDKVER%\shared" ^
    /I "%WDK%\Include\%WDKVER%\km" ^
    /I "%WDK%\Include\%WDKVER%\km\crt" ^
    /Fo"%OUT%hwmon_hv.obj" ^
    "%ROOT%hwmon_hv.c"
if errorlevel 1 goto :fail

echo Compiling hwmon_hv_driver.c (kernel mode)...
cl.exe /c /nologo /W3 /WX- /Od /Zi ^
    /D_AMD64_ /D_WIN64 /D_M_AMD64 /DDBG=0 ^
    /Gm- /EHs-c- /GR- /GF /Zp8 /GS /kernel ^
    /I "%WDK%\Include\%WDKVER%\shared" ^
    /I "%WDK%\Include\%WDKVER%\km" ^
    /I "%WDK%\Include\%WDKVER%\km\crt" ^
    /Fo"%OUT%hwmon_hv_driver.obj" ^
    "%ROOT%hwmon_hv_driver.c"
if errorlevel 1 goto :fail

echo Linking hwmon_hv.sys...
link.exe /OUT:"%OUT%hwmon_hv.sys" /NOLOGO /NODEFAULTLIB ^
    /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry ^
    /MERGE:.edata=.rdata /MERGE:.rdata=.text /SECTION:INIT,d ^
    /OPT:REF /OPT:ICF /INCREMENTAL:NO /MACHINE:X64 ^
    /LIBPATH:"%WDK%\Lib\%WDKVER%\km\x64" ^
    BufferOverflowK.lib ntoskrnl.lib hal.lib wmilib.lib ^
    "%OUT%hwmon_hv.obj" "%OUT%hwmon_hv_driver.obj" "%OUT%hwmon_hv_asm.obj"
if errorlevel 1 goto :fail

echo.
echo Hypervisor skeleton build succeeded: %OUT%hwmon_hv.sys
echo (Not loaded -- build-only, per explicit instruction. HvTryLaunch is
echo  never called, even by this driver's own DriverEntry.)
exit /b 0

:fail
echo.
echo Hypervisor skeleton build FAILED.
exit /b 1
