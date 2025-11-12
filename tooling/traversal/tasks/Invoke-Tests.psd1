@{
    # Script module or binary module file associated with this manifest.
    RootModule        = 'Invoke-Tests.psm1'

    # Version number of this module.
    ModuleVersion     = '0.1.0'

    # ID used to uniquely identify this module
    GUID              = '6f8c2ee2-6a0d-46a4-9311-4a9ab6f7ad48'

    # Author of this module
    Author            = 'abdes'

    # Company or vendor of this module
    CompanyName       = 'DroidNet'

    Copyright         = '(c) 2025 DroidNet'

    # Description of the functionality provided by this module
    Description       = 'Traversal task to run dotnet tests for projects whose name ends with .Tests (excluding .UI.Tests)'

    # Minimum version of the Windows PowerShell engine required by this module
    PowerShellVersion = '7.0'

    # Functions to export from this module
    FunctionsToExport = @(
        'Invoke-Tests'
    )

    # Cmdlets to export from this module
    CmdletsToExport   = @()

    # Variables to export from this module
    VariablesToExport = @()

    # Aliases to export from this module
    AliasesToExport   = @()

    PrivateData = @{
        PSData = @{
            Tags = @('traversal','tests','dotnet')
            ProjectUri = 'https://github.com/abdes/DroidNet'
            LicenseUri = 'https://opensource.org/licenses/MIT'
            ReleaseNotes = 'Initial manifest for Invoke-Tests task.'
        }
    }
}
