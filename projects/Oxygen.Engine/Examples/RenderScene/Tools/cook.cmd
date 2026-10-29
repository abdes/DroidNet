@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0cook.ps1" %*
exit /b %errorlevel%
