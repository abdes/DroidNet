@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0pak.ps1" %*
exit /b %errorlevel%
