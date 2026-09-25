# Repository Python environment

DroidNet uses one uv workspace, one root `uv.lock`, and one `.venv` per
checkout. All C++ build trees share that environment. Python dependencies are
declared in the package or tool-family `pyproject.toml` nearest their owner; the
root manifest selects the repository developer toolset and shared
development/build groups.

Install uv 0.12.17+ and Python 3.14, then run from the repository root:

```powershell
uv sync --locked
```

`.python-version` selects the 3.14 series; compatible patch updates remain
possible. The Python packages retain their own supported-version declarations
independently. Repository packages are editable, so code edits take effect
without reinstalling. The root lock also selects setuptools/wheel; isolated
builds of repository packages use those locked versions through uv's
`match-runtime` build dependencies.

The default selection includes the build generators, formatting/analysis tools,
repository scripts, Python tests, and image/numerical utilities. Their
dependency declarations remain local. Do not sync different subsets for
Debug/Release, ASan, Ninja or VS. The whole checkout uses the same developer
selection.

Oxygen.Engine's own `pyproject.toml` declares Gersemi, the CMake formatter. Both
`gersemi` and `python -m gersemi` are available in the shared environment. Its
version matches the upstream Gersemi pre-commit hook.

RenderDoc and FontForge scripts still use the APIs supplied by their native host
applications. Conan, uv, LLVM and other native executables are separately
installed tools; the workspace does not install competing copies of them.

## Everyday workflow

Automatic environment activation is a personal shell-profile preference. The
repository does not install or maintain shell-profile helpers. To activate
manually:

```powershell
. .venv/Scripts/Activate.ps1
# Linux/macOS: source .venv/bin/activate
```

`build-tree generate <profile>` provisions the environment through Conan setup.
`build-tree configure <preset>`, ordinary builds, `oxyformat`, `oxytidy` and the
clangd task consume it without installing packages. Launchers select their
owning checkout's interpreter instead of trusting a foreign active environment.
CMake checks the lock/environment and generator module origins before accepting
a full engine configuration. Missing/stale tooling is a setup error with a
recovery command.

VS Code's clangd task points directly at the repository `.venv`, even when the
workspace folder is `projects/Oxygen.Engine`. When editing Python itself, select
that same interpreter in the Python extension. The local formatter pre-commit
hook also consumes this environment without syncing it; third-party hooks keep
their normal pre-commit-managed environments.

## Changing requirements

Edit the owning manifest, then update and qualify the central resolution:

```powershell
uv lock
uv sync --locked
```

Commit the affected manifests and root `uv.lock` together. Routine setup uses
`--locked`, so it cannot silently rewrite dependency resolution. The old
per-tool requirements files and nested oxytools lock have been replaced by this
authority. The numerical generators retain their declared python-flint/mpmath
pins.

## Conan source exports

`conan export` generates hashed requirements for Oxygen's Python build tools
from the same workspace lock, including a separate build-backend projection and
the Python version request. They are included in the source export; no
handwritten second lock or developer-checkout paths are needed.

A full engine build from exported sources provisions a private environment for
that source build. It installs only the exported tool requirements and local
generator packages; tests add pytest only when requested. Configuration verifies
the provisioning receipt and module origins. Reusable-module cache builds do not
need the engine's generator environment. SDK consumers do not need this
workspace.

Install Python matching the exported version request and uv on the build
machine. Provisioning does not implicitly download an interpreter. A provisioned
build requires no package installation or network access during configure/build.
