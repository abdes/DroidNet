@ECHO OFF
PowerShell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\GenerateSolution.ps1" -Scope "%~dp0." -SolutionPath "%~dp0NetClassLibrary.sln" -Launch %*
EXIT /B %ERRORLEVEL%
