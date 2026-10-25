@echo off
rem Builds bin\DeskFind.exe with MSVC: Visual Studio 2022 or later (or its Build Tools) with the C++ desktop workload.
rem   build.cmd           release build
rem   build.cmd install   release build, then installs it for this user (bin\DeskFind.exe --install)
rem   build.cmd trace     bin\DeskFindTrace.exe, which logs timings to %LOCALAPPDATA%\DeskFind\trace.log
setlocal
set DEFINES=
set OUT=DeskFind
if /i "%~1"=="trace" (
    set DEFINES=/DDF_TRACE
    set OUT=DeskFindTrace
)

rem A Developer Command Prompt already has the compiler; otherwise find Visual Studio with vswhere.
where cl >nul 2>nul && goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo Visual Studio not found & exit /b 1
set VSDIR=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo Visual Studio has no C++ tools installed & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1

:build
cd /d "%~dp0"
if not exist obj\%OUT% mkdir obj\%OUT%
if not exist bin mkdir bin

rc /nologo /fo obj\app.res res\app.rc || exit /b 1

rem Pixel, text and matching loops are built for speed, everything else for size.
set FLAGS=/nologo /c /std:c++20 /GL /Gy /Gw /EHsc /utf-8 /W4 /permissive- /MT /DUNICODE /D_UNICODE %DEFINES% /Foobj\%OUT%\
cl %FLAGS% /O2 src\canvas.cpp src\text.cpp src\strings.cpp || exit /b 1
cl %FLAGS% /O1 src\app.cpp src\search.cpp src\everything.cpp src\history.cpp src\icons.cpp src\lang.cpp src\main.cpp src\selftest.cpp ^
   src\settings.cpp src\settingsui.cpp src\setup.cpp src\shell.cpp src\theme.cpp src\tray.cpp || exit /b 1

rem Static vcruntime, system UCRT (part of Windows 10+): no redistributable needed and a small exe.
link /nologo /OUT:obj\%OUT%.exe obj\%OUT%\*.obj obj\app.res /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO ^
   /NODEFAULTLIB:libucrt.lib ucrt.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib dwmapi.lib dwrite.lib advapi32.lib ^
   shlwapi.lib imm32.lib uuid.lib comctl32.lib shcore.lib || exit /b 1

copy /y obj\%OUT%.exe bin\%OUT%.exe >nul
rem Every build is a full build: the intermediates are not worth their disk space.
del /q obj\%OUT%\*.obj obj\%OUT%.exe obj\app.res 2>nul
rd obj\%OUT% obj 2>nul
if /i "%~1"=="install" (
    bin\%OUT%.exe --install
    if errorlevel 1 echo install failed & exit /b 1
)
echo ok
