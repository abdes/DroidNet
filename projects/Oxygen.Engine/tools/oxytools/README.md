# Oxygen developer tools

One Python distribution provides `oxytidy`, `oxyformat`, and optional `codemod`, with shared ownership,
validation, and file-writing code. Python 3.10+ is required.

Formatting and analysis require LLVM 23.x: `clang-format`, `clang-tidy`, and
`clang-scan-deps`. Other major versions are rejected; minor/patch releases within
23.x are supported. LLVM is installed separately from the Python package.

From the Oxygen.Engine directory, install once in your chosen interpreter:

```powershell
$toolPython = python -c "import sys; print(sys.executable)"
uv pip install --python $toolPython --editable tools/oxytools
```

When upgrading an existing `oxygen-oxytidy` or `oxygen-codemod` installation, first remove
that old distribution with `uv pip uninstall --python $toolPython oxygen-oxytidy oxygen-codemod`,
then install `oxygen-tools` using the command above. Command and Python module
names remain `oxytidy`, `oxyformat`, and `codemod`.

- [Oxytidy reference](docs/oxytidy.md)
- [Codemod usage](docs/codemod.md) and [design](docs/codemod-design.md)
- [Oxyformat usage](docs/oxyformat.md)
- [Shared repository ownership policy](../README.md#repository-ownership-policy)

Console entry points and the corresponding `python -m` commands share the same
CLI implementations. `tools/cli` contains PowerShell convenience launchers.

Install the optional codemod support when needed:

```powershell
uv pip install --python $toolPython --editable './tools/oxytools[codemod]'
```

The extra installs the Clang Python bindings and pathspec. Native libclang is
required for C++ renames; formatting and lint analysis do not import it.

## Verification

Install the codemod extra above to include all rename tests. Tests that need
missing optional dependencies or native LLVM are skipped.

Start in the Oxygen.Engine directory and keep an absolute path to the package.
Run tests from the system temporary directory: some fixtures exercise relative
paths and therefore require the working directory and temporary files to be on
the same drive. Use a system temporary directory outside any engine checkout.

```powershell
$engineRoot = (Resolve-Path .).Path
$toolsRoot = Join-Path $engineRoot 'tools/oxytools'
Push-Location ([System.IO.Path]::GetTempPath())
try {
    python -m unittest discover -s (Join-Path $toolsRoot 'tests') -v
} finally {
    Pop-Location
}
uvx ruff check $toolsRoot
uvx ruff format --check $toolsRoot
```

Real hook tests require the `pre-commit` development dependency. If it is installed
in another interpreter, set `OXYTOOLS_TEST_PRE_COMMIT_PYTHON` to that interpreter's
absolute path. Hook tests use an isolated temporary Git repository and never
alter the developer's index. LLVM integration tests require LLVM 23.x; skipped
integration tests are not validation evidence.
