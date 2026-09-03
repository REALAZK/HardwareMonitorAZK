@echo off
setlocal

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul
if errorlevel 1 (
    echo Failed to initialize MSVC environment.
    exit /b 1
)

set ROOT=%~dp0..
set OUT=%~dp0

echo Assembling primitives.asm...
ml64 /nologo /c /Fo "%OUT%primitives.obj" "%ROOT%\asm\primitives.asm"
if errorlevel 1 goto :fail

echo Compiling test_primitives.c...
cl /nologo /c /W4 /std:c17 /I "%ROOT%\include" /Fo"%OUT%test_primitives.obj" "%OUT%test_primitives.c"
if errorlevel 1 goto :fail

echo Linking...
link /nologo /out:"%OUT%test_primitives.exe" "%OUT%test_primitives.obj" "%OUT%primitives.obj" kernel32.lib
if errorlevel 1 goto :fail

echo.
echo Test build succeeded: %OUT%test_primitives.exe
"%OUT%test_primitives.exe"
exit /b %errorlevel%

:fail
echo.
echo Test build FAILED.
exit /b 1
