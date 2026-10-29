@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch.ps1" %*
exit /b %errorlevel%
