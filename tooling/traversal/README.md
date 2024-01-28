# Traversal actions

We have here a set of PowerShell modules to help traverse the projects in the
monorepo and execute 'tasks' on them. This is very helpful for working on a set
of connected projects, or in Continuous Integration workflows.

> Traversal ignores all files and directories that are ignored by `.gitignore`.

Tasks can be [built-in](#built-in-tasks) (simple tasks with very little logic in
them) or can be complex tasks implemented as full-fledged Cmdlets.

The provided [script](../traverse.ps1) is an invocation wrapper around the
[Select-Projects](./Select-Projects.psm1) Cmdlet.

## Examples

Traversing the project tree and printing the project name only, so that the
output can be used to do other things:

```shell
.\traverse.ps1 -Tasks Select-Name
.\traverse.ps1 -Tasks Select-Name -ExcludeTests
.\traverse.ps1 -Tasks Select-Name -ExcludeSamples
```

Traversing the project tree and printing the full project file path, so that the
output can be used to do other things:

```shell
.\traverse.ps1 -Tasks Select-Path
.\traverse.ps1 -Tasks Select-Path -ExcludeTests
.\traverse.ps1 -Tasks Select-Path -ExcludeSamples
```

Traversing the project tree, and if the project can be packaged as an AppX
package, run the MSBuild command to produce the package:

```shell
.\traverse.cmd -Tasks New-Package -StartLocation ..\projects -Platform x64 -Configuration Debug -PackageCertificateKeyFile "F:\projects\DroidNet\tooling\cert\DroidNet-Test.pfx" -ExcludeTests
```

> IMPORTANT NOTE: due to the limitation of parameter forwarding in PowerShell,
> only arguments passed as switches or as '-Name Value', with the parameter name
> and value separated by a space are accepted. Using ':' or '=' is not
> supported.

## Built-in tasks

- Select-Name : print the project name
- Select-Path : print the full project path

## Complex tasks

### [New-Package](tasks/New-Package.psm1)

Run MSBuild command to generate a signed AppX package for the project.

The project must have a `Package.appxmanifest` file.
Detailed usage information is in the [PowerShell module](tasks/New-Package.psm1).
