@echo off
setlocal
set "PS1UnderCmd=1"
set "CmdEnvScriptPath=%TEMP%\droidnet-init-%RANDOM%-%RANDOM%.cmd"
PowerShell -NoProfile -NoLogo -ExecutionPolicy Bypass -File "%~dp0init.ps1" %*
set "InitExit=%ERRORLEVEL%"
if not "%InitExit%"=="0" goto failed
if not exist "%CmdEnvScriptPath%" goto finished
endlocal & call "%CmdEnvScriptPath%" & del "%CmdEnvScriptPath%"
call "%~dp0.venv\Scripts\activate.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
where get-artifacts >nul 2>&1
if errorlevel 1 exit /b 1
where traverse >nul 2>&1
if errorlevel 1 exit /b 1
if "%DROIDNET_INIT_POWERSHELL_CALLER%"=="1" goto powershell
set "DROIDNET_INIT_POWERSHELL_CALLER="
echo Ready in this CMD session: get-artifacts --help; traverse --help
exit /b 0
:powershell
set "DROIDNET_INIT_POWERSHELL_CALLER="
echo Repository prepared. To enable commands in PowerShell, run .\init.ps1 or .\.venv\Scripts\Activate.ps1
exit /b 0
:finished
endlocal
exit /b 0
:failed
if exist "%CmdEnvScriptPath%" del "%CmdEnvScriptPath%"
endlocal & exit /b %InitExit%
