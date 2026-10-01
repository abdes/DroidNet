# Prepare a checkout

Install Visual Studio with the required .NET/WinUI and C++/CLI workloads, uv,
and the Python version selected by `.python-version`. Then run:

```powershell
./init.ps1
get-artifacts --help
traverse --help
```

From CMD use `init.cmd`. From PowerShell use `./init.ps1`, not the CMD launcher:
a child CMD process cannot activate commands in its parent PowerShell process.
Initialization uses the environment's standard activation scripts and verifies
command discovery before reporting readiness in the calling shell.
Initialization syncs the locked editable Python workspace, installs pre-commit
hooks, restores .NET tools and existing root/Projects solutions, and exposes
`.venv/Scripts` on this shell's PATH. It does not build projects or edit your
shell profile. Python code edits are immediately available through the commands.

```powershell
# Reuse existing installations without contacting package sources.
./init.ps1 -NoRestore -NoToolRestore -NoPythonRestore -NoPreCommitHooks

# Full option descriptions and examples.
./init.ps1 --help
```

Use `-NoRestore` for NuGet, `-NoToolRestore` for .NET tools, and
`-NoPythonRestore` for the Python workspace. `-DotNetInstall` installs the SDK
selected by `global.json`; `-Interactive` permits restore authentication prompts.
See [Python environment ownership](../PYTHON.md) and [build workflows](build.md).
