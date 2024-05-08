@ECHO OFF

SET COMMAND=dotnet slngen -d . -o Oxygen.Editor.Data.sln --folders false .\**\*.csproj
PowerShell -NoProfile -NoLogo -ExecutionPolicy unrestricted -Command "[System.Threading.Thread]::CurrentThread.CurrentCulture = ''; [System.Threading.Thread]::CurrentThread.CurrentUICulture = '';& {%COMMAND% %*}; exit $LASTEXITCODE"
