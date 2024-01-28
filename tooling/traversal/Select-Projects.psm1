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
.EXAMPLE
    Prints the full path of each selected project to the output:
    .\traverse.ps1 -Tasks Select-Path

    Create AppX packages for AppX projects:
    .\traverse.cmd -Tasks New-Package -StartLocation ..\projects -Platform x64 -Configuration Debug -PackageCertificateKeyFile "F:\projects\DroidNet\tooling\cert\DroidNet-Test.pfx" -ExcludeTests
.NOTES
    Pass parameters only as switches or using the form '-Name Value'. ';' and
    '=' are not supported.
#>
Param (
    [Parameter(HelpMessage = "Add extra diagnostic output to slngen generator.")]
    [switch]$UseDiagnostics = $false
)

# Function to load .gitignore patterns
function LoadGitIgnorePatterns() {
    $patterns = @()
    $directory = Get-Location

    # Function to convert glob patterns to regex
    function ConvertGlobToRegex {
        param([string]$glob)
        return "^$($glob -replace '\*\*', '.*' -replace '\*', '[^/]*' -replace '\?', '.')$"
    }

    while ($True) {
        $gitignore_file = Join-Path -Path $directory -ChildPath ".gitignore"

        if (Test-Path -Path $gitignore_file) {
            $patterns += Get-Content $gitignore_file |
            Where-Object { $_ -ne "" -and $_[0] -ne '#' } |
            ForEach-Object { ConvertGlobToRegex -glob $_ }
        }

        if (Test-Path -Path (Join-Path -Path $directory -ChildPath ".git")) { break }

        $directory = Split-Path -Path $directory -Parent
        if ($directory -eq "") { break }
    }

    return $patterns
}

# Function to check if a file is ignored
function CheckIgnored() {
    param(
        [string[]]$patterns,
        [System.IO.FileSystemInfo]$file
    )

    $name = $file.Name
    $relativePath = $file.FullName.Replace($startingLocation.Path + '\', '').Replace('\\', '/')
    $patternsToCheck = @($relativePath, "$relativePath/", $name, "$name/")

    foreach ($pattern in $patterns) {
        foreach ($path in $patternsToCheck) {
            if ($path -match $pattern) { return 'true' }
        }
    }
    return 'false'
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
                & "$Task" -Project $Project @OtherParams
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
        $patterns = LoadGitIgnorePatterns
        $stack = New-Object System.Collections.Stack

        Get-ChildItem -Path $StartLocation | ForEach-Object {
            if (CheckIgnored -patterns $patterns -file $_ -eq 'false') { $stack.Push($_) }
        }

        while ($stack.Count -gt 0 -and ($item = $stack.Pop())) {
            if ($item.PSIsContainer) {
                # Check if need to exclude tests or samples
                if (($ExcludeTests -and $item.Name -eq 'tests') -or ($ExcludeSamples -and $item.Name -eq 'samples')) {
                    continue
                }

                Get-ChildItem -Path $item.FullName | ForEach-Object {
                    if (CheckIgnored -file $_ -eq 'false') { $stack.Push($_) }
                }
            }
            elseif ($item.FullName -match ".*.csproj$") {
                if ($PSCmdlet.ShouldProcess($_.FullName, "Perform tasks")) {
                    foreach ($task in $Tasks) {
                        # & $knownTasks[$task] -Project $item $OtherParams
                        [scriptblock]$scriptBlock = $knownTasks[$task].action;

                        if ($knownTasks[$task].isBuiltIn) {
                            Invoke-Command -Scriptblock $scriptBlock -ArgumentList $item, $OtherParams
                        }
                        else {
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

                                    [System.Collections.Generic.List`1[System.Object]] $UnboundArgs
                                )

                                # (Incompletely) emulate PowerShell's own argument parsing by
                                # building a hashtable of parameter-argument pairs to pass through
                                # to Set-Location via splatting.
                                $htPassThruArgs = @{}; $key = $null

                                if ($UnboundArgs) {
                                    switch -regex ($UnboundArgs) {
                                        '^-(.+)' { if ($key) { $htPassThruArgs[$key] = $true } $key = $Matches[1] }
                                        default { $htPassThruArgs[$key] = $_; $key = $null }
                                    }
                                    if ($key) { $htPassThruArgs[$key] = $true } # trailing switch param.
                                }

                                & $scriptBlock $Task $Project $htPassThruArgs
                            }

                            Invoke-Command -Scriptblock $actionWrapper -ArgumentList $task, $item, $OtherParams
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
