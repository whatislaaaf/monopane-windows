@echo off
setlocal

rem Locate the latest Visual Studio with the C++ toolchain via vswhere.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe. Install Visual Studio with the C++ workload.
    exit /b 1
)

"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\mackeys_vspath.txt"
set /p VSPATH=<"%TEMP%\mackeys_vspath.txt"
del "%TEMP%\mackeys_vspath.txt"
if not defined VSPATH (
    echo No Visual Studio installation with the C++ toolchain found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist build mkdir build

rc /nologo /fo build\mackeys.res res\mackeys.rc
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /O2 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ^
   src\main.cpp src\config.cpp src\keyboard.cpp src\scancodemap.cpp src\settings.cpp ^
   build\mackeys.res ^
   /Fo:build\ /Fe:build\mackeys.exe ^
   /link user32.lib shell32.lib advapi32.lib comctl32.lib gdi32.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1

echo.
echo Built build\mackeys.exe
