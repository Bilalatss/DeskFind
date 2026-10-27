@echo off
rem Builds bin\DeskFind.exe with MSVC: Visual Studio 2022 or later (or its Build Tools) with the C++ desktop workload.
rem   build.cmd           release build
rem   build.cmd install   release build, then installs it for this user (bin\DeskFind.exe --install)
rem   build.cmd test      release build, then builds and runs bin\DeskFindTests.exe (tests\tests.cpp)
rem   build.cmd arm64     bin\DeskFind-arm64.exe for Windows on Arm (needs the MSVC ARM64 build tools)
rem   build.cmd analyze   MSVC code analysis of every source file, no exe; any warning fails it
rem   build.cmd trace     bin\DeskFindTrace.exe, which logs timings to %LOCALAPPDATA%\DeskFind\trace.log
setlocal
set MODE=%~1
set DEFINES=
set OUT=DeskFind
set ARCH=x64
if /i "%MODE%"=="trace" (
    set DEFINES=/DDF_TRACE
    set OUT=DeskFindTrace
)
if /i "%MODE%"=="arm64" (
    set ARCH=arm64
    set OUT=DeskFind-arm64
)

rem A Developer Command Prompt for the same target already has the compiler; otherwise find Visual Studio with vswhere.
if /i "%VSCMD_ARG_TGT_ARCH%"=="%ARCH%" goto build
if not defined VSCMD_ARG_TGT_ARCH if %ARCH%==x64 where cl >nul 2>nul && goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo Visual Studio not found & exit /b 1
set TOOLS=x86.x64
if %ARCH%==arm64 set TOOLS=ARM64
set VSDIR=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.%TOOLS% -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo Visual Studio has no C++ build tools for %ARCH% & exit /b 1
rem Compilers that run natively on this PC (x64 or Arm64), building for %ARCH%.
set HOST=x64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set HOST=arm64
set VCARGS=%HOST%_%ARCH%
if %HOST%==%ARCH% set VCARGS=%ARCH%
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %VCARGS% >nul 2>&1 || exit /b 1

:build
cd /d "%~dp0"
set OBJ=obj\%OUT%
set FLAGS=/nologo /c /std:c++20 /EHsc /utf-8 /W4 /permissive- /DUNICODE /D_UNICODE %DEFINES%
if /i "%MODE%"=="analyze" goto analyze
if not exist %OBJ%\main mkdir %OBJ%\main
if not exist bin mkdir bin

rc /nologo /fo %OBJ%\app.res res\app.rc || exit /b 1

rem Pixel, text and matching loops are built for speed, everything else for size. main.cpp is kept apart, so the tests
rem link everything else.
set FLAGS=%FLAGS% /GL /Gy /Gw /MT
cl %FLAGS% /Fo%OBJ%\ /O2 src\canvas.cpp src\text.cpp src\strings.cpp || exit /b 1
cl %FLAGS% /Fo%OBJ%\ /O1 src\access.cpp src\app.cpp src\appaccess.cpp src\appinput.cpp src\appview.cpp src\search.cpp src\everything.cpp src\history.cpp src\icons.cpp src\lang.cpp src\selftest.cpp ^
   src\settings.cpp src\settingsaccess.cpp src\settingsui.cpp src\setup.cpp src\shell.cpp src\theme.cpp src\tray.cpp || exit /b 1
cl %FLAGS% /Fo%OBJ%\main\ /O1 src\main.cpp || exit /b 1

rem Static vcruntime, system UCRT (part of Windows 10+): no redistributable needed and a small exe.
set LFLAGS=/nologo /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO /NODEFAULTLIB:libucrt.lib ucrt.lib user32.lib gdi32.lib shell32.lib ^
   ole32.lib oleaut32.lib dwmapi.lib dwrite.lib advapi32.lib shlwapi.lib imm32.lib uuid.lib comctl32.lib shcore.lib uiautomationcore.lib
link %LFLAGS% /OUT:%OBJ%.exe %OBJ%\*.obj %OBJ%\main\main.obj %OBJ%\app.res /SUBSYSTEM:WINDOWS || exit /b 1
copy /y %OBJ%.exe bin\%OUT%.exe >nul
if /i "%MODE%"=="test" (
    if not exist %OBJ%\tests mkdir %OBJ%\tests
    cl %FLAGS% /Fo%OBJ%\tests\ /O1 tests\tests.cpp || exit /b 1
    link %LFLAGS% /OUT:bin\DeskFindTests.exe %OBJ%\*.obj %OBJ%\tests\tests.obj %OBJ%\app.res /SUBSYSTEM:CONSOLE || exit /b 1
)
call :clean

if /i "%MODE%"=="install" (
    bin\%OUT%.exe --install
    if errorlevel 1 echo install failed & exit /b 1
)
if /i "%MODE%"=="test" (
    bin\DeskFindTests.exe
    if errorlevel 1 echo tests failed & exit /b 1
)
echo ok
exit /b 0

:analyze
rem Windows' own headers are left out of the analysis; anything it finds in DeskFind fails the build.
if not exist %OBJ% mkdir %OBJ%
cl %FLAGS% /analyze /analyze:external- /external:anglebrackets /external:W0 /WX /Fo%OBJ%\ src\*.cpp tests\tests.cpp
set FAILED=%errorlevel%
call :clean
if not %FAILED%==0 echo analysis failed & exit /b 1
echo ok
exit /b 0

:clean
rem Every build is a full build: the intermediates are not worth their disk space.
rd /s /q %OBJ% 2>nul
del /q %OBJ%.exe 2>nul
rd obj 2>nul
exit /b 0
