@echo off
rem Builds and runs the game-independent unit tests with MSVC (no CMake, no CommonLib, no game).
rem No parenthesised blocks around %ProgramFiles(x86)%: its ")" would end the block early.
setlocal
set "VSDEV="
call :try "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
call :try "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call :try "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call :try "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if defined VSDEV goto :found

rem Any other edition (Enterprise/Professional on CI runners): ask vswhere.
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSW%" goto :nomsvc
for /f "usebackq delims=" %%I in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDEV=%%I\VC\Auxiliary\Build\vcvars64.bat"

:found
if not defined VSDEV goto :nomsvc
if not exist "%VSDEV%" goto :nomsvc
call "%VSDEV%" >nul
set "OUT=%~dp0..\build_tests"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++latest /EHsc /W4 /WX /permissive- /Zc:preprocessor /utf-8 /I "%~dp0..\src" "%~dp0..\tests\test_arc.cpp" /Fe:test_arc.exe
if errorlevel 1 goto :compilefail
.\test_arc.exe
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%

:compilefail
popd
exit /b 1

:nomsvc
echo MSVC vcvars64.bat not found
exit /b 2

:try
if not defined VSDEV if exist %1 set "VSDEV=%~1"
exit /b 0
