# compute script dir (use PSScriptRoot in scripts, fallback to MyInvocation once)
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } elseif ($MyInvocation?.MyCommand?.Path) { Split-Path -Parent $MyInvocation.MyCommand.Path } else { $null }

# bail out early if we can't detect location
if (-not $scriptDir) { return }

# engine root is parent of the script folder
$engineRoot = Split-Path -Parent $scriptDir

# source VSCode shell integration if running inside vscode
if ($env:TERM_PROGRAM -eq 'vscode') { . "$(code --locate-shell-integration-path pwsh)" }

# Helper: add oxygen cli to PATH and aliases
function Add-OxygenTools {
	$oxygenCliPath = Join-Path $engineRoot 'tools\cli'
	if ((Test-Path $oxygenCliPath) -and -not ($env:PATH -split ';' | Where-Object { $_ -eq $oxygenCliPath })) {
		$env:PATH = "$oxygenCliPath;$env:PATH"
		Write-Host "Added Oxygen CLI tools to PATH: $oxygenCliPath" -ForegroundColor Green
	}
}

Set-Alias -Name oxyrun -Value (Join-Path $engineRoot 'tools\cli\oxyrun.ps1')
Set-Alias -Name oxybuild -Value (Join-Path $engineRoot 'tools\cli\oxybuild.ps1')
