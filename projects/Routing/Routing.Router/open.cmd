@ECHO OFF
PowerShell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\tooling\GenerateSolution.ps1" -Scope "%~dp0." -SolutionPath "%~dp0Routing.Router.sln" -Launch %*
EXIT /B %ERRORLEVEL%
