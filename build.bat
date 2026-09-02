@echo off
setlocal

rem Locate the latest Visual Studio with the C++ toolchain via vswhere.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe. Install Visual Studio with the C++ workload.
    exit /b 1
)

"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\monopane_vspath.txt"
set /p VSPATH=<"%TEMP%\monopane_vspath.txt"
del "%TEMP%\monopane_vspath.txt"
if not defined VSPATH (
    echo No Visual Studio installation with the C++ toolchain found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist external\keychord\keychord.h (
    echo The keychord submodule is missing. Run: git submodule update --init
    exit /b 1
)

if not exist build mkdir build

rc /nologo /fo build\monopane.res res\monopane.rc
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /O2 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ^
   src\main.cpp src\window_list.cpp src\fuzzy.cpp src\settings.cpp src\aliases.cpp ^
   src\display.cpp src\launchpad.cpp src\launchpad_apps.cpp src\paint.cpp ^
   external\keychord\keychord_name.cpp external\keychord\keychord_chord.cpp ^
   external\keychord\keychord_capture.cpp ^
   build\monopane.res ^
   /Fo:build\ /Fe:build\monopane.exe ^
   /link user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib ^
         advapi32.lib ole32.lib version.lib msimg32.lib comdlg32.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1

echo.
echo Built build\monopane.exe
