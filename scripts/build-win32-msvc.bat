@echo off
setlocal
call "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 (
  echo VsDevCmd failed, trying VS2022 BuildTools...
  call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
)
set "MAGICK_DIR="
for /d %%i in ("C:\Program Files\ImageMagick-*") do set "MAGICK_DIR=%%~fi"
if defined MAGICK_DIR set "PATH=%MAGICK_DIR%;%PATH%"
cd /d "%~dp0..\win32"
if exist build-msvc rmdir /s /q build-msvc
cmake -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 exit /b 1
cmake --build build-msvc
if errorlevel 1 exit /b 1
ctest --test-dir build-msvc --output-on-failure
exit /b %ERRORLEVEL%
