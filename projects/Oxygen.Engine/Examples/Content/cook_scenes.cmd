@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0cook_scenes.ps1" %*
exit /b %errorlevel%
