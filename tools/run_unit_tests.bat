@echo off
rem Builds and runs the game-independent unit tests with MSVC (no CMake, no CommonLib, no game).
setlocal
set "VSDEV="
for %%P in (
  "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VSDEV if exist %%P set "VSDEV=%%~P"
if not defined VSDEV ( echo MSVC vcvars64.bat not found & exit /b 2 )
call "%VSDEV%" >nul
set "OUT=%~dp0..\build_tests"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++latest /EHsc /W4 /WX /permissive- /Zc:preprocessor /utf-8 /I "%~dp0..\src" "%~dp0..\tests\test_arc.cpp" /Fe:test_arc.exe
if errorlevel 1 ( popd & exit /b 1 )
.\test_arc.exe
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
