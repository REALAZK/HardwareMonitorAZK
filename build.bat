@echo off
setlocal

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul
if errorlevel 1 (
    echo Failed to initialize MSVC environment.
    exit /b 1
)

set ROOT=%~dp0
set BUILD=%ROOT%build
if not exist "%BUILD%" mkdir "%BUILD%"

pushd "%BUILD%"

echo Assembling primitives.asm...
ml64 /nologo /c /Fo primitives.obj "%ROOT%asm\primitives.asm"
if errorlevel 1 goto :fail

echo Compiling C sources...
cl /nologo /c /W4 /std:c17 /I "%ROOT%include" ^
    "%ROOT%src\main.c" ^
    "%ROOT%src\cpu\cpu_id.c" ^
    "%ROOT%src\cpu\cpu_telemetry.c" ^
    "%ROOT%src\output\logger.c" ^
    "%ROOT%src\output\console_logger.c" ^
    "%ROOT%src\output\report.c" ^
    "%ROOT%src\firmware\smbios.c" ^
    "%ROOT%src\firmware\acpi.c" ^
    "%ROOT%src\platform\windows\firmware_table.c" ^
    "%ROOT%src\platform\windows\pci_enum_windows.c" ^
    "%ROOT%src\memory\memory_info.c" ^
    "%ROOT%src\platform\windows\storage_enum_windows.c"
if errorlevel 1 goto :fail

echo Linking...
link /nologo /out:hwmon.exe ^
    main.obj cpu_id.obj cpu_telemetry.obj logger.obj console_logger.obj report.obj primitives.obj ^
    smbios.obj acpi.obj firmware_table.obj pci_enum_windows.obj memory_info.obj storage_enum_windows.obj ^
    kernel32.lib setupapi.lib cfgmgr32.lib
if errorlevel 1 goto :fail

echo.
echo Build succeeded: %BUILD%\hwmon.exe
popd
exit /b 0

:fail
echo.
echo Build FAILED.
popd
exit /b 1
