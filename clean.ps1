Get-ChildItem -Recurse -Directory | `
  Where-Object { $_.Name -eq "obj" -or $_.Name -eq "bin" } | `
  ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
