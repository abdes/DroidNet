<#
.SYNOPSIS
    Traverses the filesystem hierarchy starting at `StartLocation` (default is
    current location), ignoring files and directories that are ignored by
    `.gitignore`, selects project file (.csproj), and runs specified taks on the
    selected projects.
.DESCRIPTION
    We have here a set of PowerShell modules to help traverse the projects in
    the monorepo and execute 'tasks' on them. This is very helpful for working
    on a set of connected projects, or in Continuous Integration workflows.

    > Traversal ignores all files and directories that are ignored by
    > `.gitignore`.

    Tasks can be [built-in](#built-in-tasks) (simple tasks with very little
    logic in them) or can be complex tasks implemented as full-fledged Cmdlets.
.PARAMETER StartLocation
    Optional start location for the traversal. Default is the current location.
.PARAMETER Tasks
    Comma separated list of tasks to run on the selected projects. At least one
    task must be provided.
.PARAMETER ExcludeTests
    When set, this switch will cause all test projects to be ignored.
.PARAMETER ExcludeSamples
    When set, this switch will cause all sample projects to be ignored.
.PARAMETER Exclude
    Optional regex to exclude projects whose file name matches the pattern.
    Example: -Exclude '^.*\.Tests$' will exclude projects whose filenames
    (without extension) end with '.Tests'.
.EXAMPLE
    Prints the full path of each selected project to the output:
    .\traverse.ps1 -Tasks Select-Path

    Create AppX packages for AppX projects:
    .\traverse.cmd -Tasks New-Package -StartLocation ..\projects -Platform x64 -Configuration Debug -PackageCertificateKeyFile "F:\projects\DroidNet\tooling\cert\DroidNet-Test.pfx" -ExcludeTests
.NOTES
    Pass parameters only as switches or using the form '-Name Value'. ';' and
    '=' are not supported.
#>
Param ()

# Function to load .gitignore patterns
function LoadGitIgnorePatterns {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.DirectoryInfo]$StartLocation
    )

    $patterns = @()
    $directory = $StartLocation

    # Function to convert glob patterns to regex
    function ConvertGlobToRegex {
        param([string]$glob)
        return "^$($glob -replace '\*\*', '.*' -replace '\*', '[^/]*' -replace '\?', '.')$"
    }

    while ($True) {
        $gitignore_file = Join-Path -Path $directory.FullName -ChildPath ".gitignore"

        if (Test-Path -Path $gitignore_file) {
            $patterns += Get-Content $gitignore_file |
            Where-Object { $_ -ne "" -and $_[0] -ne '#' } |
            ForEach-Object { ConvertGlobToRegex -glob $_ }
        }

        if (Test-Path -Path (Join-Path -Path $directory.FullName -ChildPath ".git")) { break }

        $directory = $directory.Parent
        if (-not $directory) { break }
    }

    return $patterns
}

# Function to check if a file is ignored
function CheckIgnored {
    param(
        [string[]]$patterns,
        [System.IO.FileSystemInfo]$file,
        [System.IO.DirectoryInfo]$StartLocation
    )

    $name = $file.Name
    $relativePath = $file.FullName.Replace($StartLocation.FullName + '\\', '').Replace('\\', '/')
    $patternsToCheck = @($relativePath, "$relativePath/", $name, "$name/")

    foreach ($pattern in $patterns) {
        foreach ($path in $patternsToCheck) {
            if ($path -match $pattern) { return $true }
        }
    }
    return $false
}

function Get-Tasks {
    # Add the built-in tasks
    $taskList = @{
        "Select-Name" = New-Object PSObject -Property @{
            isBuiltIn = $true
            action    = {
                param(
                    [Parameter(Mandatory)]
                    [System.IO.FileSystemInfo]$Project
                )
                Write-Output $Project.BaseName
            }
        }
        "Select-Path" = New-Object PSObject -Property @{
            isBuiltIn = $true
            action    = {
                param(
                    [Parameter(Mandatory)]
                    [System.IO.FileSystemInfo]$Project
                )
                Write-Output $Project.FullName
            }
        }
    }

    # Scan the 'tasks' folder for external tasks. Each module in 'tasks'
    # represents a task where the module name is the task name. A task module
    # exports an 'Execute-Task' function that does the work.

    $taskPath = Join-Path -Path $PSScriptRoot -ChildPath "tasks"
    $taskModules = Get-ChildItem -Path $taskPath -Filter "*.psm1"

    foreach ($module in $taskModules) {
        # Get the task name
        $taskName = $module.BaseName

        Import-Module "$($module.FullName)"

        # $scriptBlock = [scriptblock]::Create($ExecutionContext.InvokeCommand.ExpandString($scriptBlockStr))
        # Add the task to the task list
        $taskList[$taskName] = New-Object PSObject -Property @{
            isBuiltIn = $false
            action    = {
                param(
                    [Parameter(Mandatory)]
                    [string]$Task,

                    [Parameter(Mandatory)]
                    [System.IO.FileSystemInfo]$Project,

                    [System.Collections.IDictionary]
                    $OtherParams
                )
                # If the target function explicitly declares an "OtherParams" parameter,
                # prefer to pass the hashtable to it. This prevents splatting a hashtable
                # with keys unknown to the target function which would cause an error.
                $cmd = Get-Command -Name $Task -CommandType Function -ErrorAction SilentlyContinue
                # Prepare splat: always include Project explicitly
                $splat = @{
                    Project = $Project
                }

                # If the function defines explicit parameters for typical forwarding
                # like Framework and Filter, prefer to pass them directly from the
                # OtherParams hashtable so splatting doesn't fail if unknown keys
                # were provided.
                if ($cmd) {
                    foreach ($pName in @('framework','filter','verbosity')) {
                        if ($cmd.Parameters.ContainsKey(($pName.Substring(0,1).ToUpper()+$pName.Substring(1))) -and $OtherParams.Contains($pName)) {
                            $splat[($pName.Substring(0,1).ToUpper()+$pName.Substring(1))] = $OtherParams[$pName]
                        }
                    }
                }

                # If the target function still accepts a generic OtherParams hashtable,
                # pass it as well so it can receive unexpected keys.
                if ($cmd -and $cmd.Parameters.ContainsKey('OtherParams')) {
                    $splat['OtherParams'] = $OtherParams
                    & "$Task" @splat
                }
                else {
                    # Convert hashtable keys to a new hashtable with PascalCase keys
                    # for splatting to a function that expects parameter names with case
                    # matching their declaration. We still keep any $splat values.
                    $pascalSplat = @{}
                    foreach ($k in $OtherParams.Keys) {
                        $pascal = ($k.Substring(0,1).ToUpper() + $k.Substring(1))
                        $pascalSplat[$pascal] = $OtherParams[$k]
                    }
                    foreach ($key in $pascalSplat.GetEnumerator()) { $splat[$key.Key] = $key.Value }
                    & "$Task" @splat
                }
            }
        }
    }

    return $taskList
}

function Select-Projects {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [string]$StartLocation = (Get-Location),

        [switch]$ExcludeTests,

        [switch]$ExcludeSamples,
        [regex]$Exclude,

        [Parameter(Mandatory)]
        [string[]]$Tasks,

        [Parameter(ValueFromRemainingArguments = $true)]
        $OtherParams
    )

    begin {
        $knownTasks = Get-Tasks

        foreach ($task in $Tasks) {
            if (-not $knownTasks.ContainsKey($task)) {
                throw "Task '$task' is not valid. Valid tasks are: $($knownTasks.Keys -join ', ')."
            }
        }
    }

    process {
        $startingLocation = Get-Item -Path $StartLocation -ErrorAction Stop
        $patterns = LoadGitIgnorePatterns -StartLocation $startingLocation
        $stack = New-Object System.Collections.Stack

        Get-ChildItem -Path $StartLocation | ForEach-Object {
            if (-not (CheckIgnored -patterns $patterns -file $_ -StartLocation $startingLocation)) { $stack.Push($_) }
        }

        while ($stack.Count -gt 0 -and ($item = $stack.Pop())) {
            if ($item.PSIsContainer) {
                # Check if need to exclude tests or samples
                if (($ExcludeTests -and $item.Name -eq 'tests') -or ($ExcludeSamples -and $item.Name -eq 'samples')) {
                    continue
                }

                Get-ChildItem -Path $item.FullName | ForEach-Object {
                    if (-not (CheckIgnored -patterns $patterns -file $_ -StartLocation $startingLocation)) { $stack.Push($_) }
                }
            }
            elseif ($item.FullName -match ".*.csproj$") {
                # If the caller requested to exclude projects by regex and the
                # project's filename (without extension) matches the provided
                # pattern, skip processing this project.
                if ($PSBoundParameters.ContainsKey('Exclude') -and $Exclude -ne $null) {
                    try {
                        if ($Exclude.IsMatch($item.BaseName)) { continue }
                    }
                    catch {
                        Write-Error "Select-Projects: The provided -Exclude regex failed to evaluate: $($_.Exception.Message)"
                        continue
                    }
                }
                if ($PSCmdlet.ShouldProcess($item.FullName, "Perform tasks")) {
                    foreach ($task in $Tasks) {
                        # & $knownTasks[$task] -Project $item $OtherParams
                        [scriptblock]$scriptBlock = $knownTasks[$task].action;

                        if ($knownTasks[$task].isBuiltIn) {
                            if ($DebugPreference -ne 'SilentlyContinue') {
                                Invoke-Command -Scriptblock $scriptBlock -ArgumentList $item
                            }
                            else {
                                Invoke-Command -Scriptblock $scriptBlock -ArgumentList $item
                            }
                        }
                        else {
                            if ($PSBoundParameters.ContainsKey('Debug')) { Write-Debug "Select-Projects: forwarding Debug to actionWrapper for task: $task" }
                            # Wrap the action script invocation to transform the
                            # unbound arguments into a Hashtable that gets
                            # passed to the action script block.
                            # https://stackoverflow.com/questions/62622385/wrapper-function-for-cmdlet-pass-remaining-parameters/62622638#62622638
                            $actionWrapper = {
                                param(
                                            [Parameter(Mandatory)]
                                            [string]$Task,

                                            [Parameter(Mandatory)]
                                            [System.IO.FileSystemInfo]$Project,

                                        [object[]] $UnboundArgs,

                                        [bool] $ParentWhatIf,

                                        [bool] $ParentDebug
                                        )

                                # (Incompletely) emulate PowerShell's own argument parsing by
                                # building a hashtable of parameter-argument pairs to pass through
                                # to Set-Location via splatting.
                                # Build a hashtable of parameter names -> values from the
                                # remaining arguments array. This supports repeated keys
                                # (accumulated into arrays), quoted values, and boolean
                                # switches specified without a value. PowerShell already
                                # preserves quoted strings in $UnboundArgs, so we only
                                # need to interpret tokens and group them.
                                $htPassThruArgs = @{}
                                if ($UnboundArgs -and $UnboundArgs.Count -gt 0) {
                                    $i = 0
                                    while ($i -lt $UnboundArgs.Count) {
                                        $token = $UnboundArgs[$i]
                                        if ($token -is [string] -and $token -match '^(-+)(?<name>.+)$') {
                                            $name = $Matches['name']
                                            $value = $true
                                            $i++
                                            if ($i -lt $UnboundArgs.Count) {
                                                $nextToken = $UnboundArgs[$i]
                                                if (-not ($nextToken -is [string] -and $nextToken -match '^-.+')) {
                                                    # $nextToken is a value for $name
                                                    $value = $nextToken
                                                    $i++
                                                }
                                            }

                                            $name = $name.TrimStart('-')
                                            $nameKey = $name.ToLower()
                                            if ($htPassThruArgs.ContainsKey($nameKey)) {
                                                # If existing value is an arraylist, append; otherwise convert to an arraylist.
                                                if ($htPassThruArgs[$nameKey] -is [System.Collections.ArrayList]) {
                                                    [void]$htPassThruArgs[$nameKey].Add($value)
                                                }
                                                else {
                                                    $arr = New-Object System.Collections.ArrayList
                                                    [void]$arr.Add($htPassThruArgs[$nameKey])
                                                    [void]$arr.Add($value)
                                                    $htPassThruArgs[$nameKey] = $arr
                                                }
                                            }
                                            else { $htPassThruArgs[$nameKey] = $value }
                                        }
                                        else {
                                            # Ignore stray tokens that are not '-Name' tokens.
                                            $i++
                                        }
                                    }
                                }

                                if ($ParentWhatIf) { $htPassThruArgs['WhatIf'] = $true }
                                # If the parent Select-Projects was called with -Debug, forward
                                # that preference to the invoked action by providing a boolean
                                # flag the action can use to enable Write-Debug output.
                                if ($ParentDebug) { $htPassThruArgs['Debug'] = $true }

                                # If the parent requested debug, set the DebugPreference for the
                                # invoked action so Write-Debug calls are visible.
                                if ($ParentDebug) { $oldDebugPreference = $DebugPreference; $DebugPreference = 'Continue' }
                                & $scriptBlock $Task $Project $htPassThruArgs
                                if ($ParentDebug) { $DebugPreference = $oldDebugPreference }
                            }

                            if ($DebugPreference -ne 'SilentlyContinue') {
                                Invoke-Command -Scriptblock $actionWrapper -ArgumentList $task, $item, $OtherParams, $WhatIfPreference, $true
                            } else {
                                Invoke-Command -Scriptblock $actionWrapper -ArgumentList $task, $item, $OtherParams, $WhatIfPreference, $false
                            }
                        }
                    }
                }
            }
        }
    }

    end {
        foreach ($task in $knownTasks.GetEnumerator()) {
            if (!$task.Value.isBuiltIn) {
                Remove-Module $task.Key
            }
        }
    }
}
