# Repository command-line tools

Run `./init.ps1` once in your PowerShell session (or `init.cmd` in CMD). It
installs the editable workspace and exposes these commands in that shell. Each public command supports `-h` and `--help`, including
all options, defaults and usage examples.

| Command         | Purpose                                                      |
| --------------- | ------------------------------------------------------------ |
| `get-artifacts` | Query actual MSBuild output paths and optionally list files. |
| `traverse`      | Discover projects and run selected tasks.                    |

```powershell
get-artifacts -p Oxygen.Editor -c Release -j
get-artifacts -p Collections --framework-all
traverse Select-Path --start projects/Storage
traverse Invoke-Tests --start projects/Storage/tests --configuration Debug
traverse --list-tasks
```

`Invoke-Tests` runs already-built outputs, including all declared frameworks by
default. It chooses MSTest executables or VSTest according to the project.
Pass runner arguments after `--`. Failed processes and timeouts produce a failed
traversal result; `--timeout` overrides the 300-second per-process limit.

`New-Package` creates MSIX packages through Visual Studio MSBuild. It requires
an absolute signing-certificate path and builds project references. NuGet library
packing is a separate MSBuild `Pack` operation.

Shared implementation belongs in `msbuild.py`; traversal tasks remain under
`traversal/tasks/`. Add behavioral tests under `tooling/tests/`. Do not maintain
parallel guessed artifact paths or shell-specific build implementations.

See [build and analysis](../doc/build.md), [solution generation](../doc/solution-files.md),
and [artifact locations](../ARTIFACTS-README.md).
