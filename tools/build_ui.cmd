@echo off
rem Compila DNXWallpaper.UI.exe (CEF). Regenera iconos desde resources\.
setlocal
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
python "%~dp0sync_icons.py" || exit /b 1
set CEF=D:\Tools\cef120-standard\cef_binary_120.2.7+g4bc6a59+chromium-120.0.6099.234_windows64
set ROOT=D:\Develop-AI-OC_bk\REA_Implementation
set SRC=%ROOT%\wallpaper-ui-re\src
set OUT=%ROOT%\wallpaper-ui-re\DNXWallpaper.UI.exe
set BUILDOBJ=D:\Temp\opencode\build\wui
if not exist "%BUILDOBJ%" mkdir "%BUILDOBJ%"
del /q "%BUILDOBJ%\*.obj" 2>nul
cl /nologo /c /EHsc /std:c++17 /O2 /Zi /MD /DWIN32 /D_WINDOWS /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /DRE_HAVE_CEF /DCEF_USE_SANDBOX /DWRAPPING_CEF_SHARED /I"%CEF%" /I"%SRC%" /Fo"%BUILDOBJ%\\" /Fd"%BUILDOBJ%\wui.pdb" "%SRC%\main.cpp" "%SRC%\ui_windows.cpp" "%SRC%\core\scheme_factory.cpp" "%SRC%\core\method_dispatch.cpp" "%SRC%\core\file_resource_handler.cpp" "%SRC%\cef\cef_engine.cpp" "%SRC%\cef\cef_app_impl.cpp" "%SRC%\cef\cef_v8_bindings.cpp" "%SRC%\cef\projects_inventory.cpp" "%SRC%\cef\plugin_host.cpp" "%SRC%\net\lan_server.cpp" "%SRC%\editor\editor_export.cpp" || exit /b 1
rc /nologo /fo "%BUILDOBJ%\ui.res" "%ROOT%\wallpaper-ui-re\res\ui.rc" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /DEBUG /OPT:REF /OPT:ICF /MAP:"%BUILDOBJ%\wui.map" /PDB:"%BUILDOBJ%\wui.pdb" /OUT:"%OUT%" "%BUILDOBJ%\*.obj" "%BUILDOBJ%\ui.res" "%CEF%\Release\libcef.lib" "D:\Tools\cef120-standard\build\libcef_dll_wrapper.lib" shlwapi.lib advapi32.lib user32.lib kernel32.lib ole32.lib shell32.lib gdi32.lib comctl32.lib wintrust.lib crypt32.lib dwmapi.lib winmm.lib comdlg32.lib winhttp.lib || exit /b 1
copy /Y "%OUT%" "%ROOT%\dist\DNXWallpaper.UI.exe" >nul || exit /b 1
echo DNXWALLPAPER UI OK
