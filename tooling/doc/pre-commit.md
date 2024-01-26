# pre-commit

A framework for managing and maintaining multi-language pre-commit hooks.

For more information see: <https://pre-commit.com/>.

## Quick Start

### 1. Install pre-commit and pre-requisites

```shell
pip install pre-commit
```

Install [dotnet-format](https://github.com/dotnet/format) and
[XamlStyler](https://github.com/Xavalon/XamlStyler) locally, so that the
versions used by `pre-commit` stay consistent with what is used in the repo
command lines.

```shell
dotnet tool install dotnet-format
dotnet tool install xamlstyler.console
```

> The previous step can also be done by restoring the tools configured in the
> manifest. For that to work as expected, the manifest needs to have the
> following tool entry:

```json
  "tools": {
      ...
      "dotnet-format": {
          "version": "5.1.250801",
          "commands": ["dotnet-format"]
      },
      "xamlstyler.console": {
          "version": "3.2311.2",
          "commands": ["xstyler"]
      }
      ...
  }
```

### 2. Add configuration

```yaml
repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: end-of-file-fixer
      - id: trailing-whitespace

  - repo: https://github.com/compilerla/conventional-pre-commit
    rev: v3.1.0
    hooks:
      - id: conventional-pre-commit
        stages: [commit-msg]
        args: []

  - repo: local
    hooks:
    # Use dotnet format already installed locally
    - id: dotnet-format
      name: C# dotnet-format
      language: system
      entry: dotnet format --include
      types_or: ["c#"]

  - repo: local
    hooks:
    # Use XamlStyler already installed locally as a dotnet tool
    - id: xaml-styler
      name: XAML styler
      language: system
      entry: dotnet xstyler -f
      types: [file]
      files: \.xaml$
```

### 3. Install the git hook scripts

```shell
pre-commit install
```

### 4. Run against all the files (optional)

It's usually a good idea to run the hooks against all of the files when adding new hooks (usually pre-commit will only run on the changed files during git hooks).

```shell
pre-commit run --all-files
```

## Auto-update

Auto-update pre-commit config to the latest repos' versions.

```shell
pre-commit autoupdate
```

## Auto install

The repo can automatically install `pre-commit` and its hooks as part of the
`Init` script. It is done by default, unless the option `NoPrecommitInstall` is
passed to the script.

When the installation is successful, a lock file (`.pre-commit.installed.lock`)
is created at the repo's root. This file is not under source control, and is
used to subsequently skip the installation of pre-commit and its hooks.

Additionally, through a common property group in the `Directory.build.props`
file at the repo's root, every time a build is done, `MSBuild`` will check if
the lock file exists, and if it does not, it will install`pre-commit` hooks.

Through these two mechanisms, the risk of using the repo without the hooks in
place is almost zero.

```xml
<PropertyGroup>
    <PreCommitInstallLockFilePath>$(MSBuildThisFileDirectory)\.pre-commit.installed.lock</PreCommitInstallLockFilePath>
</PropertyGroup>
<Target Name="MSBumpBeforeBuild" BeforeTargets="BeforeBuild"
    Condition="!Exists($(PreCommitInstallLockFilePath))">
    <Touch Files="$(PreCommitInstallLockFilePath)" AlwaysCreate="true" />
    <Exec Command="pre-commit install" />
</Target>
```
