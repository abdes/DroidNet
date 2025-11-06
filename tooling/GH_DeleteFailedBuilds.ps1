gh run list --repo abdes/DroidNet --status failure --limit 1000 --json databaseId |
  ConvertFrom-Json |
  ForEach-Object {
    $id = $_.databaseId
    Write-Host "Deleting run $($id)..."
    $resp = gh api --method DELETE "/repos/abdes/DroidNet/actions/runs/$($id)" 2>&1
    if ($LASTEXITCODE -eq 0) {
      Write-Host "Deleted run $($id)" -ForegroundColor Green
    } else {
      Write-Host "Failed to delete run $($id):" -ForegroundColor Yellow
      Write-Host $resp
    }
    Start-Sleep -Milliseconds 200
  }
