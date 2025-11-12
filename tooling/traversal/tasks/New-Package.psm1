<#
.SYNOPSIS
    Run MSBuild command to generate a signed AppX package for the project.
.DESCRIPTION
    Provides a way to package AppX applications from the command line, mainly
    for CI and batch packaging of multiple projects.
.PARAMETER Configuration
    Specifies the build configuration (e.g. Debug, Release, etc.). Default is
    'Debug'.
.PARAMETER Platform
    Specifies the target platform. Default is 'x64'.
.PARAMETER PackageLocation
    Directory where the package will be placed. Default is under 'Packages' in
    the same directory than the project file.
.PARAMETER PackageCertificateKeyFile
    Required. Specifies the absolute path to the signing certificate file to use
    to sign the package.
.PARAMETER Project
    A positional parameter that should contain the path to the project file, for
    which a package needs to be created.
.NOTES
    See https://github.com/MicrosoftDocs/windows-dev-docs/blob/docs/hub/apps/package-and-deploy/ci-for-winui3.md
#>
Param ()

function New-Package {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [string]$Configuration = 'Debug',
        [string]$Platform = 'x64',
        [string]$PackageLocation,
        [string]$Verbosity = 'normal',

        [Parameter(Mandatory)]
        [string]$PackageCertificateKeyFile,

        [Parameter(Mandatory, Position = 0)]
        [System.IO.FileSystemInfo]$Project
    )

    begin {
        # If we don't get a PackageLocation, put the package under 'Packages' in
        # the same directory than the project file.
        if (-not $PackageLocation) {
            $PackageLocation = Join-Path -Path $Project.DirectoryName -ChildPath 'Packages'
        }

        # Ensure that the PackageLocation ends with a path separator character.
        if (-not $PackageLocation.EndsWith([IO.Path]::DirectorySeparatorChar)) {
            $PackageLocation += [IO.Path]::DirectorySeparatorChar
        }

        # Check the certificate file path is absolute. For some misterious
        # reason, if the path is not absolute, the signing fails and the app
        # cannot be installed.
        if (![System.IO.Path]::IsPathRooted($PackageCertificateKeyFile)) {
            throw "Certificate file path '$PackageCertificateKeyFile' must be absolute."
        }

        # Check the certificate file exists
        if (-not (Test-Path -Path $PackageCertificateKeyFile)) {
            throw "Certificate file '$PackageCertificateKeyFile' does not exist."
        }
    }

    process {
        # Check that project has a Package.appxmanifest, otherwise just skip it
        $manifest = Join-Path -Path $Project.DirectoryName -ChildPath 'Package.appxmanifest'
        if (-not (Test-Path -Path $manifest -PathType Leaf)) {
            return
        }

        $MsBuildParameters = @{
            Configuration = $Configuration

            # You must specify a platform because the default value may be `Any
            # CPU`, and it's not possible to build AppX packages for it. Note
            # that this traversal does not support AppX bundling.
            Platform = $Platform

            UapAppxPackageBuildMode = 'SideloadOnly'
            AppxBundle = 'Never'

            # This is what will make MSBuild generate the AppX package
            GenerateAppxPackageOnBuild = 'true'

            # You must pass the `/p:BuildProjectReferences=false` parameter to
            # `msbuild`, so that it does not try to package referenced projects.
            BuildProjectReferences = 'false'


            PackageCertificateKeyFile = $PackageCertificateKeyFile
            AppxPackageDir = $PackageLocation
        }

        $arguments = $MsBuildParameters.GetEnumerator() | ForEach-Object { "/p:$($_.Name)=$($_.Value)" }
        if ($PSCmdlet.ShouldProcess($Project.FullName, "Create application package")) {
            &"msbuild" $Project.FullName -v:$Verbosity $arguments
        }
    }
}
