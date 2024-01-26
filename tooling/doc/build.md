# Build

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
