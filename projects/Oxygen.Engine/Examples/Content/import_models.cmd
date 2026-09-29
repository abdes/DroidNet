@echo off
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0import_models.ps1" %*
exit /b %errorlevel%
