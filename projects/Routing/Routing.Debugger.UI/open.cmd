@ECHO OFF
PowerShell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\tooling\GenerateSolution.ps1" -Scope "%~dp0." -SolutionPath "%~dp0Routing.Debugger.UI.sln" -Launch %*
EXIT /B %ERRORLEVEL%
