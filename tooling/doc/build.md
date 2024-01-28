# Build

TODO(abdes): RANDOM NOTES TO BE CLEANED UP

## Packaged WinUI App

<https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/ci-for-winui3?WT.mc_id=WD-MVP-5001077&pivots=winui3-packaged-csharp>

<https://github.com/microsoft/github-actions-for-desktop-apps>

```yaml
# Build the Windows Application Packaging project
- name: Build the Windows Application Packaging Project (wapproj)
  run: msbuild $env:Solution_Path /p:Platform=$env:TargetPlatform /p:Configuration=$env:Configuration /p:UapAppxPackageBuildMode=$env:BuildMode /p:AppxBundle=$env:AppxBundle /p:PackageCertificateKeyFile=$env:SigningCertificate /p:PackageCertificatePassword=${{ secrets.Pfx_Key }}
  env:
    AppxBundle: Never
    BuildMode: SideloadOnly
    Configuration: Debug
    TargetPlatform: ${{ matrix.targetplatform }}
```

If you intend to upload your package to the store, you don't need to sign it with your own certificate.

msbuild .\samples\PackagedApp\PackagedApp.csproj /p:Configuration=Debug /p:Platform=x64 /p:UapAppxPackageBuildMode=SideloadOnly /p:AppxBundle=Never /p:AppxPackageDir="Packages" /p:GenerateAppxPackageOnBuild=true /p:runtime=win10-x64 /p:BuildProjectReferences=false /p:PackageCertificateKeyFile=Hosting.DemoApp_TemporaryKey.pfx

TODO: Check is <PublishReadyToRun>true</PublishReadyToRun> is needed.

```
    # Decode the base 64 encoded pfx and save the Signing_Certificate
    - name: Decode the pfx
      run: |
        $pfx_cert_byte = [System.Convert]::FromBase64String("${{ secrets.BASE64_ENCODED_PFX }}")
        $certificatePath = "GitHubActionsWorkflow.pfx"
        [IO.File]::WriteAllBytes("$certificatePath", $pfx_cert_byte)
```

> Use Windows PowerShell to run the Install.ps1 script.

```text
Add-AppDevPackage.resources/
Dependencies/
Add-AppDevPackage.ps1
Hosting.DemoApp_1.0.0.0_x64_Debug.cer
Hosting.DemoApp_1.0.0.0_x64_Debug.msix
Hosting.DemoApp_1.0.0.0_x64_Debug.msixsym
>> Install.ps1 <<
```

```shell
 .\Install.ps1 -SkipLoggingTelemetry
 ```
