@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0pak_content.ps1" %*
exit /b %errorlevel%
