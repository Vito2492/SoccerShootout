@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Could not initialize Visual Studio 2022 BuildTools x64 environment.
    exit /b 1
)
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%~dp0src" /I"%~dp0src\win_sdl2" ^
   "%~dp0src\soccer.c" ^
   /Fe:"%~dp0SoccerShootout.exe" ^
   /Fo:"%~dp0src\soccer.obj" ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup ^
   "%~dp0src\win_sdl2\SDL2.lib" comdlg32.lib user32.lib
if errorlevel 1 exit /b 1
del /q "%~dp0src\soccer.obj" 2>nul
echo Built SoccerShootout.exe successfully!
