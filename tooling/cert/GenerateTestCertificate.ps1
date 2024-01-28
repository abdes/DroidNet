<#
.SYNOPSIS
    Use this script to create a test certificate.

.DESCRIPTION
    All Windows app packages must be digitally signed before they can be
    deployed. While using Visual Studio 2022, it's easy to package and publish
    an app, but that process does not go well with automation.

    This certificate can be used locally during testing to sign app packages
    with the MSBuild command to create them. This scenario si an alternate way
    to do the dev/test lifecycle without the Visual Studio UI.

    Most importantly, the generated certificate is used as a secret in the
    GitHub CI workflow.

.NOTES
    The script also remove sthe generated certificate from the certificate store
    so tha it can be added later when installing an application signed with it.

    To install the certificate and make it trusted, simply used the
    `Install.ps1` script provided in one of the AppX packaged signed with it.
#>

# Create an expiery for the certificate.
$todaydt = Get-Date
$expiry = $todaydt.AddMonths(48)

# Generate the certificate.
$NewSelfSignedCertificateParameters = @{
    Subject           = "CN=DroidNet"
    Type              = "Custom"
    KeyUsage          = "DigitalSignature"
    NotAfter          = $expiry
    FriendlyName      = "DroidNet Package Signing Test Certificate"
    CertStoreLocation = "Cert:\CurrentUser\My"
    TextExtension     = @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
}

$cert = New-SelfSignedCertificate @NewSelfSignedCertificateParameters

# Export the certificate as a pfx code signing key.
$fileOut = Join-Path $PSScriptRoot DroidNet-Test.pfx
$certificateBytes = $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pkcs12)
[System.IO.File]::WriteAllBytes($fileOut, $certificateBytes)

# Clean up the certificate from the Windows Certificate Store.
Get-ChildItem Cert:\CurrentUser\My |
Where-Object { $_.Thumbprint -match $cert.Thumbprint } |
Remove-Item
