Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Wait-ForStableFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [int]$TimeoutSeconds = 30,
    [int]$PollMilliseconds = 250,
    [int]$StableSamplesRequired = 3
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $lastLength = -1L
  $lastWriteTimeUtc = [datetime]::MinValue
  $stableSamples = 0

  while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $Path) {
      $item = Get-Item -LiteralPath $Path
      if ($item.Length -eq $lastLength -and $item.LastWriteTimeUtc -eq $lastWriteTimeUtc) {
        $stableSamples++
      } else {
        $lastLength = $item.Length
        $lastWriteTimeUtc = $item.LastWriteTimeUtc
        $stableSamples = 1
      }

      if ($stableSamples -ge $StableSamplesRequired) {
        return
      }
    }

    Start-Sleep -Milliseconds $PollMilliseconds
  }

  throw "Timed out waiting for file to stabilize: $Path"
}

function Get-CaptureSnapshot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Directory,

    [Parameter(Mandatory = $true)]
    [string]$Filter
  )

  $snapshot = @{}
  if (-not (Test-Path -LiteralPath $Directory)) {
    return $snapshot
  }

  Get-ChildItem -LiteralPath $Directory -Filter $Filter -File | ForEach-Object {
    $snapshot[$_.FullName] = @{
      Length = $_.Length
      LastWriteTimeUtc = $_.LastWriteTimeUtc
    }
  }

  return $snapshot
}

function Get-NewOrUpdatedCaptures {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Directory,

    [Parameter(Mandatory = $true)]
    [string]$Filter,

    [Parameter(Mandatory = $true)]
    [hashtable]$Before
  )

  if (-not (Test-Path -LiteralPath $Directory)) {
    return @()
  }

  $captures = Get-ChildItem -LiteralPath $Directory -Filter $Filter -File
  return @(
    $captures | Where-Object {
      $previous = $Before[$_.FullName]
      if ($null -eq $previous) {
        return $true
      }

      $_.Length -ne $previous.Length -or $_.LastWriteTimeUtc -ne $previous.LastWriteTimeUtc
    } | Sort-Object @(
      @{
        Expression = {
          if ($_.BaseName -match '_frame(\d+)$') {
            return [int]$matches[1]
          }

          return [int]::MaxValue
        }
      },
      @{ Expression = 'Name' }
    )
  )
}

function Get-LastDirectionalSummary {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $searchResults = @(
    Select-String -Path $Path -Pattern 'directional light summary total=(\d+).*shadowed_total=(\d+).*shadowed_sun=(\d+)'
  )
  if ($searchResults.Count -eq 0) {
    return $null
  }

  $last = $searchResults[-1]
  return [pscustomobject]@{
    Total = [int]$last.Matches[0].Groups[1].Value
    ShadowedTotal = [int]$last.Matches[0].Groups[2].Value
    ShadowedSun = [int]$last.Matches[0].Groups[3].Value
    Line = $last.Line.Trim()
  }
}

function Get-LastTextureRepoint {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $searchResults = @(
    Select-String -Path $Path -Pattern 'TextureBinder\.cpp:1112\s+\|\s+Repointed descriptor'
  )
  if ($searchResults.Count -eq 0) {
    return $null
  }

  $last = $searchResults[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-LastSceneBuild {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $searchResults = @(Select-String -Path $Path -Pattern 'RenderScene: Scene build staged successfully')
  if ($searchResults.Count -eq 0) {
    return $null
  }

  $last = $searchResults[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-CaptureRequest {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [int]$Frame
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $pattern = "RenderDoc configured frame capture requested for frame $Frame"
  $searchResults = @(Select-String -Path $Path -Pattern $pattern)
  if ($searchResults.Count -eq 0) {
    return $null
  }

  $last = $searchResults[-1]
  return [pscustomobject]@{
    LineNumber = $last.LineNumber
    Line = $last.Line.Trim()
  }
}

function Get-LogTimeOfDay {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Line
  )

  $match = [regex]::Match($Line, '^(?<time>\d{2}:\d{2}:\d{2}\.\d{3})')
  if (-not $match.Success) {
    return $null
  }

  return [timespan]::ParseExact($match.Groups['time'].Value, 'hh\:mm\:ss\.fff', $null)
}

function Get-FrameMarkerAfterLine {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [int]$LineNumber
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $searchResults = @(Select-String -Path $Path -Pattern 'Renderer: frame=Frame\(seq:(\d+)\)')
  foreach ($match in $searchResults) {
    if ($match.LineNumber -gt $LineNumber) {
      return [pscustomobject]@{
        LineNumber = $match.LineNumber
        Frame = [int]$match.Matches[0].Groups[1].Value
        Line = $match.Line.Trim()
      }
    }
  }

  return $null
}

function Get-LatestMatch {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [string]$Pattern
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $matches = @(Select-String -Path $Path -Pattern $Pattern)
  if ($matches.Count -eq 0) {
    return $null
  }

  return $matches[-1].Line.Trim()
}
