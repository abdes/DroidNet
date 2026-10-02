# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause

<#
.SYNOPSIS
    Writes the installed SDK's runtime receipt and compile-input fingerprint.
.DESCRIPTION
    Hashes native DLLs, headers and import libraries. Writes each generated header
    only when its content changes; never invokes a compiler or test runner.
.PARAMETER SdkRoot
    Installed engine SDK root containing bin, include and lib.
.PARAMETER Configuration
    Debug or Release; selects the required engine DLL names.
.PARAMETER OutputHeader
    Runtime receipt header consumed by AssemblyInfo.cpp.
.PARAMETER CompileHeader
    Header/library fingerprint consumed by all Interop translation units.
.PARAMETER Help
    Show options and usage. Aliases: -h and --help.
.EXAMPLE
    ./Write-NativeSdkReceipt.ps1 -SdkRoot H:/sdk/Release -Configuration Release -OutputHeader H:/obj/receipt.h -CompileHeader H:/obj/compile.h
    Generate both headers from a Release SDK installation.
#>
[CmdletBinding(DefaultParameterSetName="Generate")]
param(
    [Parameter(Mandatory, ParameterSetName="Generate")][string] $SdkRoot,
    [Parameter(Mandatory, ParameterSetName="Generate")][ValidateSet('Debug', 'Release')][string] $Configuration,
    [Parameter(Mandatory, ParameterSetName="Generate")][string] $OutputHeader,
    [Parameter(Mandatory, ParameterSetName="Generate")][string] $CompileHeader,
    [Parameter(ParameterSetName="Help")][Alias("h", "-help")][switch] $Help
)
if ($Help) { Get-Help $PSCommandPath -Full; return }
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
$content = "#pragma once`n#define OXYGEN_NATIVE_SDK_RECEIPT L`"$literal`"`n"
$OutputHeader = [IO.Path]::GetFullPath($OutputHeader)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputHeader))
if (!(Test-Path -LiteralPath $OutputHeader) -or [IO.File]::ReadAllText($OutputHeader) -cne $content) {
    [IO.File]::WriteAllText($OutputHeader, $content, [Text.UTF8Encoding]::new($false))
}

$CompileHeader = [IO.Path]::GetFullPath($CompileHeader)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($CompileHeader))
$compileContent = "#pragma once`n// SDK compile inputs: $compileHash`n"
if (!(Test-Path -LiteralPath $CompileHeader) -or [IO.File]::ReadAllText($CompileHeader) -cne $compileContent) {
    [IO.File]::WriteAllText($CompileHeader, $compileContent, [Text.UTF8Encoding]::new($false))
}
