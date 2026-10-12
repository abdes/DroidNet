@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_scene.ps1" %*
exit /b %errorlevel%
