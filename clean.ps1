Get-ChildItem -Recurse -Directory | `
  Where-Object { $_.Name -eq "obj" -or $_.Name -eq "bin" -or $_.Name -eq "artifacts" } | `
  ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
