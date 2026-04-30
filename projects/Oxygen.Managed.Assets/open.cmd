@ECHO OFF

SET COMMAND=slngen -d . -o Oxygen.Managed.Assets.sln --folders false .\**\*.csproj
PowerShell -NoProfile -NoLogo -ExecutionPolicy unrestricted -Command "[System.Threading.Thread]::CurrentThread.CurrentCulture = ''; [System.Threading.Thread]::CurrentThread.CurrentUICulture = '';& {%COMMAND% %*}; exit $LASTEXITCODE"
