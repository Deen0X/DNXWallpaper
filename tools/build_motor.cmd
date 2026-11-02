@echo off
rem Compila DNXWallpaper.Motor.exe. Regenera iconos desde resources\.
setlocal
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
python "%~dp0sync_icons.py" || exit /b 1
set ROOT=D:\Develop-AI-OC_bk\REA_Implementation
set SRC=%ROOT%\wallpaper-motor-re\src
set OUT=%ROOT%\wallpaper-motor-re\DNXWallpaper.Motor.exe
set BUILDOBJ=D:\Temp\opencode\build\motor
if not exist "%BUILDOBJ%" mkdir "%BUILDOBJ%"
del /q "%BUILDOBJ%\*.obj" 2>nul
cl /nologo /c /EHsc /std:c++17 /O2 /MD /DWIN32 /D_WINDOWS /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /I"%SRC%" /Fo"%BUILDOBJ%\\" "%SRC%\main.cpp" "%SRC%\wallpaper_manager.cpp" "%SRC%\tray.cpp" "%SRC%\wizard.cpp" "%SRC%\ipc\ipc.cpp" "%SRC%\ipc\reload_watcher.cpp" "%SRC%\render\dwm_attach.cpp" "%SRC%\render\d3d11_renderer.cpp" "%SRC%\media\scene_wallpaper.cpp" "%SRC%\media\video_wallpaper.cpp" "%SRC%\media\material_system.cpp" "%SRC%\media\audio.cpp" "%SRC%\media\image_load.cpp" || exit /b 1
rc /nologo /fo "%BUILDOBJ%\motor.res" "%ROOT%\wallpaper-motor-re\res\motor.rc" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OUT:"%OUT%" "%BUILDOBJ%\*.obj" "%BUILDOBJ%\motor.res" d3d11.lib d3dcompiler.lib dxgi.lib dcomp.lib dwmapi.lib mfreadwrite.lib mfplat.lib mfuuid.lib mf.lib shlwapi.lib windowscodecs.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib kernel32.lib wtsapi32.lib shell32.lib gdi32.lib comctl32.lib || exit /b 1
copy /Y "%OUT%" "%ROOT%\dist\DNXWallpaper.Motor.exe" >nul || exit /b 1
echo DNXWALLPAPER MOTOR OK
