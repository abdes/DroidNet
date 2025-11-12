@{
    # Script module or binary module file associated with this manifest.
    RootModule        = 'Select-Projects.psm1'

    # Version number of this module.
    ModuleVersion     = '0.1.0'

    # ID used to uniquely identify this module
    GUID              = '3e8b3b9f-1d8b-4f1b-82dc-3c2b8d0c3b24'

    # Author of this module
    Author            = 'abdes'

    # Company or vendor of this module
    CompanyName       = 'DroidNet'

    Copyright         = '(c) 2025 DroidNet'

    # Description of the functionality provided by this module
    Description       = 'Traversal cmdlet that locates CSProj files across the repo and forwards tasks to per-project task modules.'

    # Minimum version of the Windows PowerShell engine required by this module
    PowerShellVersion = '7.0'

    # Functions to export from this module
    FunctionsToExport = @(
        'Select-Projects'
    )

    # Cmdlets to export from this module
    CmdletsToExport   = @()

    # Variables to export from this module
    VariablesToExport = @()

    # Aliases to export from this module
    AliasesToExport   = @()

    PrivateData = @{
        PSData = @{
            Tags = @('traversal','project-selection','tasks')
            ProjectUri = 'https://github.com/abdes/DroidNet'
            LicenseUri = 'https://opensource.org/licenses/MIT'
            ReleaseNotes = 'Expanded PSData for the Select-Projects cmdlet manifest.'
        }
    }
}
