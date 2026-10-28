#requires -Version 7.4
# Model import records under <ContentRoot>/imports, shared by the Content
# scripts. One source file has one record: a second record for the same source
# (for example one made under an older virtual path naming) imports and packages
# the model twice, under two virtual paths.

# Returns each record's resolved source path, virtual path and record path,
# keyed by the source path (case-insensitive).
function Get-ModelImportRecordsBySource([Parameter(Mandatory)][string]$ContentRoot) {
    $bySource = [Collections.Generic.Dictionary[string, Collections.Generic.List[object]]]::new([StringComparer]::OrdinalIgnoreCase)
    $records = Join-Path $ContentRoot 'imports'
    if (-not (Test-Path -LiteralPath $records -PathType Container)) {
        return $bySource
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $records -Filter '*.import.json' -File | Sort-Object Name)) {
        $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        # A record's source is relative to the record's folder, or absolute.
        $source = [IO.Path]::GetFullPath($record.source, $file.DirectoryName)
        if (-not $bySource.ContainsKey($source)) {
            $bySource[$source] = [Collections.Generic.List[object]]::new()
        }
        $bySource[$source].Add([pscustomobject]@{
                Record = $file.FullName
                Source = $source
                VirtualPath = $record.virtual_path
            })
    }
    return $bySource
}

# Throws, naming every source with more than one record, so neither an import
# nor a package runs with a model recorded twice.
function Assert-OneModelImportRecordPerSource([Parameter(Mandatory)][string]$ContentRoot) {
    $bySource = Get-ModelImportRecordsBySource $ContentRoot
    $duplicates = @($bySource.Values | Where-Object { $_.Count -gt 1 })
    if ($duplicates.Count -eq 0) {
        return
    }
    $lines = foreach ($records in $duplicates) {
        "  $($records[0].Source):"
        foreach ($entry in $records) { "    $([IO.Path]::GetFileName($entry.Record)) ($($entry.VirtualPath))" }
    }
    throw "These model sources have more than one import record under $(Join-Path $ContentRoot 'imports'). Keep one record per source and delete the others:`n$($lines -join "`n")"
}
