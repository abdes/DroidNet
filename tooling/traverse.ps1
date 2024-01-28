#!/usr/bin/env pwsh

<#
.SYNOPSIS
    Wrapper invocation script for the `Select-Project` traversal Cmdlet. Simply
    forwards all arguments to the Cmdlet.
.DESCRIPTION
    With this script, we can do traversal of the project tree without having to
    import the CmdLet in the current environment. This will be done
    automatically by this script, and once finished, the module will be removed.
#>

# Import the module in the script
$modulePath = Join-Path -Path $PSScriptRoot -ChildPath "traversal\Select-Projects.psm1"
Import-Module $modulePath

try {
    Select-Projects @args
}
finally {
    # Remove the module at the end of the script
    Remove-Module -Name 'Select-Projects'
}
