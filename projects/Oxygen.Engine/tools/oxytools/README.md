# Oxygen developer tools

One Python distribution provides `oxytidy`, `oxyformat`, and optional `codemod`, with shared ownership,
validation, and file-writing code. Python 3.10+ is required.

Formatting and analysis require LLVM 23.x: `clang-format`, `clang-tidy`, and
`clang-scan-deps`. Other major versions are rejected; minor/patch releases within
23.x are supported. LLVM is installed separately from the Python package.

The tools belong to the repository's shared uv workspace. From the DroidNet root:

```powershell
uv sync --locked
```

The checked-in `.python-version` selects Python 3.14. Tool dependencies stay in
this package's `pyproject.toml`; exact versions and build backends are resolved by
the repository `uv.lock`. The PowerShell launchers use this checkout's root
`.venv` even when another environment is active. They never install packages.
See the repository's `tooling/PYTHON.md` for the complete setup workflow.

- [Oxytidy reference](docs/oxytidy.md)
- [Codemod usage](docs/codemod.md) and [design](docs/codemod-design.md)
- [Oxyformat usage](docs/oxyformat.md)
- [Shared repository ownership policy](../README.md#repository-ownership-policy)

Console entry points and the corresponding `python -m` commands share the same
CLI implementations. `tools/cli` contains PowerShell convenience launchers.

The repository developer environment includes the codemod Python extra.

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

Real hook tests require the `pre-commit` development dependency. For isolated tests only, if it is installed
in another interpreter, set `OXYTOOLS_TEST_PRE_COMMIT_PYTHON` to that interpreter's
absolute path. Hook tests use an isolated temporary Git repository and never
alter the developer's index. LLVM integration tests require LLVM 23.x; skipped
integration tests are not validation evidence.
