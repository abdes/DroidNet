# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause

# Generates compile input for the existing Interop project. No compiler or test runner is launched.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $SdkRoot,
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string] $Configuration,
    [Parameter(Mandatory)][string] $OutputHeader
)
$ErrorActionPreference = 'Stop'
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
function Get-SdkFileHash([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$nativeFiles = @(Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'bin') -Filter '*.dll' -File | Sort-Object Name)
$suffix = if ($Configuration -eq 'Debug') { '-d' } else { '' }
foreach ($required in @("Oxygen.Engine$suffix.dll", "Oxygen.Engine.EditorInterface$suffix.dll")) {
    if ($required -notin $nativeFiles.Name) { throw "Install the $Configuration engine SDK first: $required is missing." }
}
$artifacts = @($nativeFiles | ForEach-Object {
    [ordered]@{ id = 'engine/bin/' + $_.Name; size = $_.Length; sha256 = Get-SdkFileHash $_.FullName; schemaId = $null }
})
$receipt = [ordered]@{ version = 1; configuration = $Configuration; artifacts = $artifacts } | ConvertTo-Json -Depth 5 -Compress
# Header/import-library changes also invalidate compiled C++ code even when DLLs are unchanged.
$compileFiles = @(Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'include'), (Join-Path $SdkRoot 'lib') -Recurse -File | Sort-Object FullName)
$compileIdentity = ($compileFiles | ForEach-Object {
    $_.FullName.Substring($SdkRoot.Length).Replace('\', '/') + '|' + (Get-SdkFileHash $_.FullName)
}) -join "`n"
$sha = [Security.Cryptography.SHA256]::Create()
try { $compileHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($compileIdentity))).Replace('-', '') }
finally { $sha.Dispose() }
$literal = $receipt.Replace('\', '\\').Replace('"', '\"')
$content = "#pragma once`n// SDK compile inputs: $compileHash`n#define OXYGEN_NATIVE_SDK_RECEIPT L`"$literal`"`n"
$OutputHeader = [IO.Path]::GetFullPath($OutputHeader)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputHeader))
if (!(Test-Path -LiteralPath $OutputHeader) -or [IO.File]::ReadAllText($OutputHeader) -cne $content) {
    [IO.File]::WriteAllText($OutputHeader, $content, [Text.UTF8Encoding]::new($false))
}
