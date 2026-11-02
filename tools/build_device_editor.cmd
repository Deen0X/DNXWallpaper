@echo off
rem Compila device-editor.exe. Regenera iconos desde resources\.
setlocal
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
python "%~dp0sync_icons.py" || exit /b 1
set ROOT=D:\Develop-AI-OC_bk\REA_Implementation
set SRC=%ROOT%\device-editor-re\src
set RES=%ROOT%\device-editor-re\res
set OUT=%ROOT%\device-editor-re\device-editor.exe
set BUILDOBJ=D:\Temp\opencode\build\device-editor
if not exist "%BUILDOBJ%" mkdir "%BUILDOBJ%"
del /q "%BUILDOBJ%\*.obj" 2>nul
cl /nologo /c /EHsc /std:c++17 /O2 /MD /DWIN32 /D_WINDOWS /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /I"%SRC%" /Fo"%BUILDOBJ%\\" "%SRC%\main.cpp" || exit /b 1
rc /nologo /fo "%BUILDOBJ%\device-editor.res" "%RES%\device-editor.rc" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OUT:"%OUT%" "%BUILDOBJ%\*.obj" "%BUILDOBJ%\device-editor.res" shlwapi.lib winhttp.lib user32.lib shell32.lib advapi32.lib ole32.lib || exit /b 1
copy /Y "%OUT%" "%ROOT%\dist\device-editor.exe" >nul || exit /b 1
echo DEVICE EDITOR OK
