@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0upload_apk.ps1" %*
