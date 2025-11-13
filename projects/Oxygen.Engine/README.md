# Oxygen Game Engine

## Install latest VC Redistributable Package

**Optimized version crashes on Mutex machinery in the STL.**

<https://developercommunity.visualstudio.com/t/Visual-Studio-17100-Update-leads-to-Pr/10669759?sort=newest>
I’m resolving it as By Design, as explained in our release notes:

Fixed mutex’s constructor to be constexpr.
Note: Programs that aren’t following the documented restrictions on binary compatibility may encounter null dereferences in mutex machinery. You must follow this rule:
When you mix binaries built by different supported versions of the toolset, the Redistributable version must be at least as new as the latest toolset used by any app component.

You can define \_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR as an escape hatch.
That is, if you’re seeing crashes due to null dereferences in mutex locking machinery, you’re deploying a program built with new STL headers, but without a sufficiently new msvcp140.dll, which is unsupported. You need to be (re)distributing a new STL DLL too. (If a VS 2022 17.10 VCRedist has been independently installed on the machine - then everything will happen to work.)

Solve the problem based on Karel Van de Rostyne’s comment:

**The solution for this problem is:**
Download the latest Microsoft Visual C++ Redistributables and install them on
the machine that gives the problem.

On this Microsoft site you find the downloads.
<https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170>

## Shader Compilation Setup

[text](https://github.com/Devaniti/GetDXC)

## Dev Setup

### PowerShell

$env:VIRTUAL_ENV_DISABLE_PROMPT = 1
oh-my-posh init pwsh --config E:\dev\ohmyposh-config.json | Out-String | Invoke-Expression
function AutoActivateVenv {
$venvActivate = ".venv\Scripts\Activate"
if (Test-Path $venvActivate) {
& $venvActivate
}
}

function Set-Location {
param ([string]$Path)
Microsoft.PowerShell.Management\Set-Location -Path $Path
AutoActivateVenv
}

## Python venv

cd dev/projects
python -m venv .venv

.venv/Scripts/activate

## Pre-commit

./Init.cmd

## Visual Studio

Make sure the "Desktop development with C++" workload is checked.
After installation, check for vcvarsall.bat in:

```pwsh
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\
```

## Conan

cd dev/projects

pip install conan
git clone <https://github.com/abdes/conan-center-index.git>

```shell
$ conan remote remove conancenter

# Add the mycenter remote pointing to the local folder
$ conan remote add mycenter ./conan-center-index

$ cd DroidNet/projects/Oxygen.Engine

# $ conan install . --profile:host=profiles/windows-msvc-asan.ini --profile:build=profiles/windows-msvc-asan.ini --build=missing -s build_type=Debug --deployer-folder=out/install --deployer-package=Oxygen/0.1.0

$ conan install . --profile:host=profiles/windows-msvc.ini --profile:build=profiles/windows-msvc.ini --build=all -s build_type=Debug --deployer-folder=out/install --deployer-package=Oxygen/0.1.0 -c tools.microsoft.msbuild:vs_version=18
```

## Useful commands

```powershell
$repoRoot=$(git rev-parse --show-toplevel); git diff --name-only --cached | Where-Object { $_ -match '\.(h|cpp)$' } | ForEach-Object { $abs=Join-Path $repoRoot $_; clang-format -i $abs; Write-Output "Formatted: $abs" }
```

```powershell
$repoRoot=$(git rev-parse --show-toplevel); git diff --name-only --cached | Where-Object { $_ -match '(CMakeLists\.txt|\.cmake)$' } | ForEach-Object { $abs=Join-Path $repoRoot $_; gersemi -i $abs; Write-Output "Formatted: $abs" }
```

## Developer notes: running the BindlessCodeGen CLI

The bindless codegen tool is provided as a small library and a CLI entrypoint. To avoid a Python runtime warning when running the CLI directly, prefer invoking it as a module from a clean interpreter process:

```powershell
& F:/projects/.venv/Scripts/python.exe -m bindless_codegen.cli --input <path-to-BindingSlots.yaml> --out-cpp out.h --out-hlsl out.hlsl
```

Notes:

- The package uses lazy imports for submodules (no import-time side-effects), so `python -m bindless_codegen.cli` is the recommended invocation for development. Installing a console_scripts entrypoint (via setup/pyproject) is also a convenient option for CI and developer workflows.
- If you see a RuntimeWarning from runpy about modules found in sys.modules, it means the interpreter already had the package imported; running the CLI in a fresh process will avoid that.
