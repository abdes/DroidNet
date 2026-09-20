# Oxygen developer tools

One Python distribution provides `oxytidy` and `oxyformat`, with shared ownership,
validation, and file-writing code. Python 3.10+ is required.

From the Oxygen.Engine directory, install once in your chosen interpreter:

```powershell
$toolPython = python -c "import sys; print(sys.executable)"
uv pip install --python $toolPython --editable tools/oxytools
```

When upgrading an existing editable `oxygen-oxytidy` installation, first remove
that old distribution with `uv pip uninstall --python $toolPython oxygen-oxytidy`,
then install `oxygen-tools` using the command above. Command and Python module
names remain `oxytidy` and `oxyformat`.

- [Oxytidy reference](docs/oxytidy.md)
- [Oxyformat contract and usage](docs/oxyformat.md)
- [Shared repository ownership policy](../README.md#repository-ownership-policy)

Console entry points and `python -m oxytidy` / `python -m oxyformat` share the same
CLI implementations. `tools/cli` contains PowerShell convenience launchers.

## Verification

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
alter the developer's index. LLVM integration tests require LLVM 22.x; skipped
integration tests are not validation evidence.
