<#
.SYNOPSIS
    Invoke tests for '.Tests' projects (excluding '.UI.Tests') using dotnet run.
.DESCRIPTION
    This task accepts forwarded parameters such as -Framework (alias -tfm),
    -Filter and other arguments. If those arguments are not declared by name,
    they can be collected via the -OtherParams hashtable.
#>

Param ()

function Invoke-Tests {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory = $false)]
        [Alias('tfm')]
        [string]
        $Framework,

        [Parameter(Mandatory = $false)]
        # No alias for Filter to avoid alias/name conflict
        [string]
        $Filter,

        [Parameter(Mandatory = $false)]
        [string]
        $Verbosity = 'minimal',

        [Parameter(Mandatory = $false)]
        [hashtable]
        $OtherParams,



        [Parameter(Mandatory = $true, Position = 0)]
        [System.IO.FileSystemInfo]
        $Project
    )

    begin {
        # Decide whether this project should be considered a test project (exclude UI tests)
        $isTestProject = $Project.BaseName -match '\.Tests$' -and (-not ($Project.BaseName -match '\.UI\.Tests$'))
    }

    process {
        if (-not $isTestProject) { return }
        Write-Debug "Considering project: $($Project.BaseName)"
        if (-not $isTestProject) { Write-Debug "Skipping non-test project: $($Project.BaseName)"; return }

        # Build dotnet arguments. Use --project and --framework flags plus filter
        $dotnetArgs = @('run', '--no-build', '--project', $Project.FullName)

        if ($Framework) {
            $dotnetArgs += @('--framework', $Framework)
        }
        else {
            # Try to auto-detect the Target Framework (TFM) used by the project.
            # Use msbuild /pp to expand imports (Directory.Build.props), falling back to raw XML parsing.
            function Get-TargetFrameworkFromProject([string]$ProjectPath) {
                $tfm = $null
                $ppFile = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), [System.IO.Path]::GetRandomFileName() + '.pp')
                try {
                    Write-Debug "Attempting msbuild preprocess to detect TFM for $ProjectPath"
                    $argList = @('msbuild', $ProjectPath, "/nologo", "/pp:$ppFile", '/v:q')
                    $startInfo = @{ FilePath = 'dotnet'; ArgumentList = $argList; NoNewWindow = $true; PassThru = $true; Wait = $true }
                    Start-Process @startInfo | Out-Null
                    if (Test-Path $ppFile) {
                        $content = Get-Content $ppFile -Raw
                        try { $xml = [xml]$content } catch { $xml = $null }
                        if ($xml) {
                            # Find the first non-empty TargetFramework or TargetFrameworks property
                            $nodes = $xml.SelectNodes('//TargetFramework|//TargetFrameworks')
                            foreach ($node in $nodes) {
                                if (-not [string]::IsNullOrWhiteSpace($node.InnerText)) {
                                    $tf = $node.InnerText.Trim()
                                    if ($tf) { $tfm = $tf; break }
                                }
                            }
                        }
                    }
                }
                catch {
                    Write-Debug "msbuild /pp attempt failed: $($_.Exception.Message)"
                }
                finally { if (Test-Path $ppFile) { Remove-Item $ppFile -Force -ErrorAction SilentlyContinue } }

                if (-not $tfm) {
                    # Fallback: parse original csproj
                    try {
                        $content = Get-Content $ProjectPath -Raw
                        try { $xml = [xml]$content } catch { $xml = $null }
                        if ($xml) {
                            $nodes = $xml.SelectNodes('//TargetFramework|//TargetFrameworks')
                            foreach ($node in $nodes) {
                                if (-not [string]::IsNullOrWhiteSpace($node.InnerText)) {
                                    $tf = $node.InnerText.Trim()
                                    if ($tf) { $tfm = $tf; break }
                                }
                            }
                        }
                    }
                    catch { Write-Debug "Fallback project parsing failed: $($_.Exception.Message)" }
                }

                if ($tfm -and $tfm -match ';') { $tfm = ($tfm -split ';')[0].Trim() }

                # If it looks like a Target Framework Version string (e.g. v4.7.2), map it to net472.
                if ($tfm -and $tfm -match '^v\d+\.\d+') {
                    $ver = $tfm.TrimStart('v') -replace '\.', ''
                    $tfm = "net$ver"
                }
                return $tfm
            }

            # detect and set the framework if possible
            $detectedFramework = Get-TargetFrameworkFromProject -ProjectPath $Project.FullName
            if ($detectedFramework) {
                Write-Debug "Detected Target Framework: $detectedFramework"
                $dotnetArgs += @('--framework', $detectedFramework)
            }
            else { Write-Debug "No Target Framework detected; proceeding without --framework" }
        }

        if ($Filter) { $dotnetArgs += @('--filter', $Filter) }

        if ($Verbosity) { $dotnetArgs += @('--verbosity', $Verbosity) }

        # Include other named parameters that were forwarded via the hashtable
        if ($OtherParams) {
            Write-Debug "OtherParams keys: $($OtherParams.Keys -join ',')"
            # Avoid duplicating Framework/Filter values that are supplied via dedicated parameters
            $excludedKeys = @('framework','tfm','filter','project')
            # Normalize keys
            $keysToAdd = @()
            foreach ($k in $OtherParams.Keys) {
                if (-not ($excludedKeys -contains $k.ToLower())) { $keysToAdd += $k }
            }

            foreach ($k in $keysToAdd) {
                Write-Debug "Adding key: $k value: $($OtherParams[$k])"
                $val = $OtherParams[$k]
                switch ($val) {
                    # When a boolean true, add the switch only
                    ($true) { $dotnetArgs += "--$k"; break }
                    default {
                        if ($val -is [System.Collections.IEnumerable] -and -not ($val -is [string])) {
                            foreach ($v in $val) { $dotnetArgs += @("--$k", $v) }
                        }
                        else { $dotnetArgs += @("--$k", $val) }
                    }
                }
            }
        }

        Write-Debug "Constructed command: dotnet $($dotnetArgs -join ' ')"
        for ($i = 0; $i -lt $dotnetArgs.Length; $i++) { Write-Debug "dotnetArgs[$i] = $($dotnetArgs[$i])" }
        # Remove duplicate flag/value pairs (e.g. repeated --Foo Bar) that may have
        # been added accidentally due to forwarded args being duplicated.
        $finalArgs = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $dotnetArgs.Length; $i++) {
            $token = $dotnetArgs[$i]
            if ($token -is [string] -and $token.StartsWith('--')) {
                # check for a flag-value pair
                if ($i + 1 -lt $dotnetArgs.Length -and -not ($dotnetArgs[$i + 1] -is [string] -and $dotnetArgs[$i + 1].StartsWith('--'))) {
                    $pair = @($token, $dotnetArgs[$i + 1])
                    # check for existence by pair
                    $exists = $false
                    for ($j = 0; $j -lt $finalArgs.Count; $j++) {
                        if ($finalArgs[$j] -eq $pair[0] -and $j + 1 -lt $finalArgs.Count -and $finalArgs[$j + 1] -eq $pair[1]) { $exists = $true; break }
                    }
                    if (-not $exists) { [void]$finalArgs.Add($pair[0]); [void]$finalArgs.Add($pair[1]) }
                    $i++ # skip value
                    continue
                }
                else {
                    # boolean switch, only add if not present
                    if (-not ($finalArgs -contains $token)) { [void]$finalArgs.Add($token) }
                    continue
                }
            }
            else {
                # Positional argument - add if not present
                if (-not ($finalArgs -contains $token)) { [void]$finalArgs.Add($token) }
            }
        }

        Write-Debug "Final command: dotnet $($finalArgs -join ' ')"
        for ($i = 0; $i -lt $finalArgs.Count; $i++) { Write-Debug "finalArgs[$i] = $($finalArgs[$i])" }
        if ($PSCmdlet.ShouldProcess($Project.FullName, "Run tests")) {
            & dotnet @finalArgs
        }
    }

    end {}
}

Export-ModuleMember -Function Invoke-Tests
