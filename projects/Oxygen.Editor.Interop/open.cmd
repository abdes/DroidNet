@ECHO OFF

SET COMMAND=dotnet slngen -d . -o EditorInterop.sln --folders false .\**\*.*proj
PowerShell -NoProfile -NoLogo -ExecutionPolicy unrestricted -Command "[System.Threading.Thread]::CurrentThread.CurrentCulture = ''; [System.Threading.Thread]::CurrentThread.CurrentUICulture = '';& {%COMMAND% %*}; exit $LASTEXITCODE"
