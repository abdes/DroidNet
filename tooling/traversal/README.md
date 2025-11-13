# Traversal actions

A set of PowerShell modules to traverse project trees and execute "tasks" on
projects in the monorepo. This is helpful for locally running commands across
multiple projects or in Continuous Integration workflows.

> Traversal ignores files and folders that are ignored by the repository's
> `.gitignore` files.

Tasks may be either built-in or authored as external Cmdlets (placed under the
`tooling/traversal/tasks` folder). The `traverse.ps1` script is a wrapper that
invokes the `Select-Projects` cmdlet to locate projects and run tasks on them.

## Usage (examples)

From the repository root, call the traversal wrapper script:

```pwsh
# Print project names
.\tooling\traverse.ps1 -Tasks Select-Name
.\tooling\traverse.ps1 -Tasks Select-Name -ExcludeTests
.\tooling\traverse.ps1 -Tasks Select-Name -ExcludeSamples

# Print full project path
.\tooling\traverse.ps1 -Tasks Select-Path
.\tooling\traverse.ps1 -Tasks Select-Path -ExcludeTests
.\tooling\traverse.ps1 -Tasks Select-Path -ExcludeSamples

# Build AppX package for package-capable projects
.\tooling\traverse.ps1 -Tasks New-Package -StartLocation .\projects -Platform x64 -Configuration Debug -PackageCertificateKeyFile "F:\projects\DroidNet\tooling\cert\DroidNet-Test.pfx" -ExcludeTests

# Run tests for .Tests projects (excludes .UI.Tests by default)
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Framework net9.0-windows10.0.26100.0 -Filter 'Category!=UITest' -Debug -WhatIf
# Example: pass the Configuration to influence `--configuration` for `dotnet run`
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Configuration Debug -Filter 'Category!=UITest'
```

Notes:

- `-StartLocation` is optional; default is the repository root.

- `-Debug` enables `Write-Debug` output in tasks (diagnostics); `-Verbose` is also supported.

- `-WhatIf` and `-Confirm` are forwarded so you can simulate runs safely.

## Argument forwarding and supported patterns

`Select-Projects` forwards arguments to external task modules. This forwarding
has been improved to support the following cases:

- Support for both single-dash (`-name`) and double-dash (`--name`) tokens.
- Quoted values are preserved (for example `-Configuration "My Config"`).
- Repeated keys are forwarded as an array: `-Extra A -Extra B` becomes
    `Extra = ["A","B"]`.
- Boolean switches are forwarded as `Flag = $true`.
- The parser builds a hashtable of parameters and either splats into the
    function or passes the whole hashtable as `-OtherParams` if the task declares
    such a parameter.

Known limitations:

- The parser assumes a `-Name Value` tokenization pattern. Values that
    begin with `-` (e.g. `-Configuration -Debug`) will be interpreted as a
    parameter unless the value is quoted (e.g. `-Configuration "-Debug"`).

- The `name:value` or `name=value` syntaxes are not supported; please use
    `-Name Value` or `--name value` instead.

When writing a custom external task, add a `Project` parameter (Type: `FileInfo`
or `FileSystemInfo`) and any named optional parameters you want to accept. For
forwarding arbitrary unknown keys without function signature mismatch, declare
an `OtherParams` parameter that expects a hashtable. Example:

```powershell
function New-Package {
        param(
                [Parameter(Mandatory)]
                [System.IO.FileSystemInfo]$Project,

                [string]$Platform,

                [string]$Configuration,

                [hashtable]$OtherParams
        )
        # ...
}
```

If `OtherParams` is present, the parser will pass the entire argument hashtable
as `-OtherParams` to your function. Otherwise, parsed keys will be splatted.

### Parameter normalization

- Parameter names are normalized to lower-case in the parser, and when splatted
    to tasks they are converted to PascalCase to align with typical PowerShell
    parameter naming.

- The traversal wrapper also forwards `-Debug`, `-Verbose`, `-WhatIf`, and `-Confirm`.

## Built-in tasks

### New-Package (tooling/traversal/tasks/New-Package.psm1)

Runs MSBuild to create an AppX package when the project includes
`Package.appxmanifest`. Use `-WhatIf` to verify behavior without producing
packages. Detailed options and flags are in the module's help header.

### Invoke-Tests (tooling/traversal/tasks/Invoke-Tests.psm1)

Runs `dotnet run` for test projects whose names end with `.Tests` — `*.UI.Tests`
projects are excluded by default. This task is intended for CI flows that need
to run many test runner projects and accepts forwarded parameters such as
`-Framework` (TFM) and `-Filter`.

Example (keeping UI tests out):

```pwsh
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Framework net9.0-windows10.0.26100.0 -Filter 'Category!=UITest' -Debug
```

Example: pass the Configuration to influence `--configuration` for `dotnet run`:

```pwsh
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Configuration Debug -Filter 'Category!=UITest'
```

You can use aliases: `-Config`, `-c`, or `-configuration` are accepted too.

You can also pass application-level arguments that should go after the `--`
dotnet separator via `-app` (alias for `-AppArgs`):

```pwsh
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Configuration Debug -Filter 'Category!=UITest' -app '--no-ansi','--no-progress'
```

`Invoke-Tests` builds a `dotnet run` argument array and forwards known named
parameters; duplicate values are de-duplicated before running `dotnet`.

## Writing external tasks

- Place one or more task modules under `tooling/traversal/tasks/` as `*.psm1`.
- Provide a `.psd1` manifest (recommended) so the module exports are explicit.
- Tasks should declare a `Project` parameter and any other named parameters.
- If you expect to receive arbitrary or unknown keys, declare an `OtherParams`
    [hashtable] parameter to receive the argument hashtable.

## Tips / FAQ

- Use `-WhatIf` and `-Debug` for safe debugging and verbose diagnostics:

```pwsh
.\tooling\traverse.ps1 -Tasks Invoke-Tests -StartLocation .\projects -Framework net9.0-windows10.0.26100.0 -Filter 'Category!=UITest' -Debug -WhatIf
```

- For `dotnet run`, some projects target multiple frameworks — pass `-Framework`
    to disambiguate which target to run.

- If you hit an issue where values starting with `-` are misinterpreted, wrap
    the value in quotes to ensure it is treated as a value (ex: `-Filter "-MyFlag"`).

- Use `OtherParams` if you want to handle unknown keys yourself inside the task.

## Contributing

If you add a new task, please provide tests where possible and add a module
manifest (`.psd1`) to the task under `tooling/traversal/tasks`. This makes the
task self-describing and avoids issues with automatic discovery.

---

Happy traversing! 🚀
