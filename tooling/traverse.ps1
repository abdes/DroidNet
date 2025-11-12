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
    # Forward common switches (Debug, Verbose, WhatIf, Confirm) to Select-Projects
    $forward = @()
    if ($PSBoundParameters.ContainsKey('Debug')) { $forward += '-Debug' }
    if ($PSBoundParameters.ContainsKey('Verbose')) { $forward += '-Verbose' }
    if ($PSBoundParameters.ContainsKey('WhatIf')) { $forward += '-WhatIf' }
    if ($PSBoundParameters.ContainsKey('Confirm')) { $forward += '-Confirm' }

    Select-Projects @args $forward
}
finally {
    # Remove the module at the end of the script
    Remove-Module -Name 'Select-Projects'
}
