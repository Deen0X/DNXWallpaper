@echo off
rem Compila DNXWallpaper.exe (launcher). Regenera iconos desde resources\.
setlocal
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
python "%~dp0sync_icons.py" || exit /b 1
set ROOT=D:\Develop-AI-OC_bk\REA_Implementation
set SRC=%ROOT%\wallpaper-launcher-re\src
set RES=%ROOT%\wallpaper-launcher-re\res
set OUT=%ROOT%\wallpaper-launcher-re\DNXWallpaper.exe
set BUILDOBJ=D:\Temp\opencode\build\launcher
if not exist "%BUILDOBJ%" mkdir "%BUILDOBJ%"
del /q "%BUILDOBJ%\*.obj" 2>nul
cl /nologo /c /EHsc /std:c++17 /O2 /MD /DWIN32 /D_WINDOWS /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /I"%SRC%" /Fo"%BUILDOBJ%\\" "%SRC%\launcher.cpp" "%SRC%\distribution.cpp" "%SRC%\file_verify.cpp" "%SRC%\manifest.cpp" || exit /b 1
rc /nologo /fo "%BUILDOBJ%\launcher.res" "%RES%\launcher.rc" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OUT:"%OUT%" "%BUILDOBJ%\*.obj" "%BUILDOBJ%\launcher.res" shlwapi.lib advapi32.lib shell32.lib user32.lib gdi32.lib comctl32.lib bcrypt.lib || exit /b 1
copy /Y "%OUT%" "%ROOT%\dist\DNXWallpaper.exe" >nul || exit /b 1
echo DNXWALLPAPER LAUNCHER OK
