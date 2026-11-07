@echo off
REM Builds NAJI DISTORTION.vst3 on Windows.
REM Requires: Visual Studio 2022 (with "Desktop development with C++"), CMake, Git.

cmake -S . -B build -A x64
if errorlevel 1 goto :fail

cmake --build build --config Release --target NajiDistortion_VST3 --parallel
if errorlevel 1 goto :fail

echo.
echo Done. Your plugin is here:
echo   build\NajiDistortion_artefacts\Release\VST3\NAJI DISTORTION.vst3
echo Copy that whole "NAJI DISTORTION.vst3" folder to C:\Program Files\Common Files\VST3\
pause
exit /b 0

:fail
echo Build failed - see the messages above.
pause
exit /b 1
