param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
  [string]$ReportPath = "",
  [switch]$IncludeDocs,
  [switch]$IncludeTooling
)

$ErrorActionPreference = "Stop"

$forbidden = @(
  @{
    Name = "legacy renderer include"
    Pattern = [regex]'#\s*include\s*[<"]Oxygen/Renderer/'
  },
  @{
    Name = "legacy renderer namespace"
    Pattern = [regex]'\boxygen::renderer\b|\bnamespace\s+oxygen::renderer\b'
  },
  @{
    Name = "legacy renderer qualified symbol"
    Pattern = [regex]'\brenderer::'
  },
  @{
    Name = "legacy renderer target"
    Pattern = [regex]'\boxygen-renderer\b|\boxygen::renderer\b|\bOxygen\.Renderer\b'
  }
)

$sourceRoots = @(
  "src\Oxygen\Vortex",
  "Examples\Async",
  "Examples\DemoShell",
  "Examples\InputSystem",
  "Examples\LightBench",
  "Examples\TexturedCube",
  "Examples\RenderScene",
  "Examples\MultiView",
  "Examples\Physics",
  "Examples\VortexBasic"
)

if ($IncludeTooling) {
  $sourceRoots += "tools\vortex"
}

if ($IncludeDocs) {
  $sourceRoots += @(
    "src\Oxygen",
    "Examples",
    "tools\vortex"
  )
}

$sourceExtensions = @(
  ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
  ".cmake", ".txt", ".ps1"
)

if ($IncludeDocs) {
  $sourceExtensions += @(".md", ".rst")
}

$excludedDirectories = @(
  "\.git\",
  "\out\",
  "\build\",
  "\bin\",
  "\obj\",
  "\.vs\",
  "\.planning\"
)

function Test-IsExcludedPath {
  param([string]$Path)
  $normalized = $Path.Replace("/", "\")
  foreach ($excluded in $excludedDirectories) {
    if ($normalized.Contains($excluded)) {
      return $true
    }
  }
  return $false
}

$matches = New-Object System.Collections.Generic.List[object]
$scannedFiles = 0
$normalizedRepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd("\", "/")

function Get-RelativePath {
  param([string]$Path)
  $normalizedPath = (Resolve-Path -LiteralPath $Path).Path
  if ($normalizedPath.StartsWith($normalizedRepoRoot,
      [System.StringComparison]::OrdinalIgnoreCase)) {
    return $normalizedPath.Substring($normalizedRepoRoot.Length).TrimStart("\", "/")
  }
  return $normalizedPath
}

foreach ($root in $sourceRoots) {
  $absoluteRoot = Join-Path $RepoRoot $root
  if (-not (Test-Path -LiteralPath $absoluteRoot)) {
    continue
  }

  Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File | ForEach-Object {
    $file = $_
    if ($PSCommandPath -ne $null -and
      $file.FullName.Equals($PSCommandPath,
        [System.StringComparison]::OrdinalIgnoreCase)) {
      return
    }
    if (Test-IsExcludedPath -Path $file.FullName) {
      return
    }
    if ($sourceExtensions -notcontains $file.Extension) {
      return
    }

    $scannedFiles++
    $relativePath = Get-RelativePath -Path $file.FullName
    $lineNumber = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
      $lineNumber++
      foreach ($rule in $forbidden) {
        if ($rule.Pattern.IsMatch($line)) {
          $matches.Add([pscustomobject]@{
              Rule = $rule.Name
              Path = $relativePath
              Line = $lineNumber
              Text = $line.Trim()
            })
        }
      }
    }
  }
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add("Vortex legacy renderer seam scan")
$report.Add("RepoRoot: $RepoRoot")
$report.Add("Scanned files: $scannedFiles")
$report.Add("IncludeDocs: $IncludeDocs")
$report.Add("IncludeTooling: $IncludeTooling")
$report.Add("")

if ($matches.Count -eq 0) {
  $report.Add("Result: PASS")
  $report.Add("No forbidden current-source legacy renderer seams found.")
} else {
  $report.Add("Result: FAIL")
  $report.Add("Forbidden seams:")
  foreach ($match in $matches) {
    $report.Add(
      ("{0}:{1}: {2}: {3}" -f $match.Path, $match.Line, $match.Rule,
        $match.Text))
  }
}

if ($ReportPath -ne "") {
  $reportDirectory = Split-Path -Parent $ReportPath
  if ($reportDirectory -ne "" -and -not (Test-Path -LiteralPath $reportDirectory)) {
    New-Item -ItemType Directory -Path $reportDirectory | Out-Null
  }
  Set-Content -LiteralPath $ReportPath -Value $report -Encoding UTF8
}

$report | ForEach-Object { Write-Host $_ }

if ($matches.Count -ne 0) {
  exit 1
}
