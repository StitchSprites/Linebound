@echo off
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%
if not exist build mkdir build
cl /std:c++20 /EHsc /nologo /DUNICODE /D_UNICODE /DNDEBUG /Fo:build\ main.cpp /Fe:build\Linebound.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib