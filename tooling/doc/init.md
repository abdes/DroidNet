# Development environment

## Prerequisites

* Recent version of Visual Studio: [Community 2022](https://visualstudio.microsoft.com/vs/community/)
* Recent version of Python: [Python 3.12](https://www.python.org/downloads/)
* Latest .Net SDK: [.Net 8](https://dotnet.microsoft.com/en-us/download)

## Init

To build this repository from the command line, and to leverage most of its
automation and QoL features, you must first execute our init.ps1 script, which
does the following things:

* Install [pre-commit](https://pre-commit.com/) and its hooks
* Restores NuGet packages,
* Restores .Net tools,
* Can install .Net SDK in CI pipelines,\

Assuming your working directory is the root directory of this git repo, and you
are running Windows PowerShell, the command is:

```powershell
./init.ps1
```

### CI Pipeline Init

Inside a CI pipeline, such as GitHub Actions, use the following command to
install the .Net SDK and initialize the project at once. Note that SDK install
is optional, and must be explicitly enabled with the `DotNetInstall` option.

```cmd
./init -DotNetInstall
```

Any environment variables, and any modifications to the `PATH`, will
automatically be made available inside the pipeline environment.
