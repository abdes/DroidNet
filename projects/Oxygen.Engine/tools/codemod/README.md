# Oxygen Codemod

Oxygen Codemod is a specialized refactoring tool designed for the **Oxygen Engine**. It provides AST-aware symbol renames for C++ and regex-based updates for shaders, data files, and documentation.

## Key Features

- **Semantic Overload Resolution (C++)**: Uses `libclang` to distinguish between overloaded functions and namespaced symbols.
- **Multi-Language Drivers**:
  - **C++**: AST-aware renames for classes, methods, variables, and namespaces.
  - **HLSL**: Regex-based token replacement for shaders (`.hlsl`, `.hlsli`).
  - **JSON/Scene**: Key/Value updates for structured data and engine scenes.
  - **Text**: Fallback for documentation (`.md`, `.txt`).
- **Safety First**: Instead of modifying files directly, the tool generates **Unified Diff (.patch)** files.
  - `safe.patch`: Confident, semantically verified changes.
  - `review.patch`: Ambiguous matches (e.g., name collisions) that require manual verification.
- **Blazing Fast**: Uses `ripgrep` for ultra-fast symbol discovery across the repo (respects `.gitignore` by default).

## Prerequisites

- **Python 3.10+**
- **LLVM/Clang**: Required for C++ semantic analysis.
  - Ensure `libclang.dll` (Windows) or `libclang.so` (Linux) is in your PATH or standard LLVM installation directory.

## Installation

```powershell
cd tools/codemod
pip install -e .
```

## Usage

### Basic Rename

Rename a class or function across the repository:

```powershell
codemod --from OldSymbol --to NewSymbol --kind class --output-safe-patch rename.patch
```

### Dry Run

Preview what would be changed without writing a patch:

```powershell
codemod --from OldSymbol --to NewSymbol --kind function --dry-run -v
```

### Targeting Overloads

To rename a specific overload or namespaced function, use the fully qualified name:

```powershell
codemod --from "Oxygen::Renderer::Initialize" --to "Startup" --kind function
```

### Compilation Database Support

The tool automatically detects `compile_commands.json` by searching for a `.clangd` configuration file in the project root. This enables **headless, compiler-accurate AST parsing** with correct include paths and defines.

#### Configuration (.clangd)

You can customize the compiler flags passed to `libclang` by adding a `.clangd` file in your project root. This is useful for stripping incompatible flags (like MSVC-specific ones) or adding required standards.

```yaml
CompileFlags:
  CompilationDatabase: "out/build-ninja" # Path to folder containing compile_commands.json
  Remove: [-MDd, -fno-rtti]              # Flags to strip
  Add: [-std=c++20, -fms-extensions]     # Flags to append
```

**Note**: The tool respects the order: `Remove` rules are applied first, then `Add` rules are inserted (safely handling `--` separators).

### Advanced Filtering (Globs)

Use `--include` and `--exclude` with gitignore-style glob patterns to precisely target files:

```powershell
# Only process headers in the Renderer module, excluding tests
codemod --from Old --to New --include "src/Oxygen/Renderer/**/*.h" --exclude "**/*Test*"
```

## Command Line Arguments

| Argument | Description |
| :--- | :--- |
| `--from` | The symbol name to find. |
| `--to` | The new symbol name. |
| `--kind` | (Optional) Symbol kind: `class`, `function`, `variable`, `namespace`. |
| `--root` | (Optional) Root directory to search. Defaults to current working directory, or auto-detects project root via `.clangd`/`.git`. |
| `--include` | Glob pattern to include (repeatable). |
| `--exclude` | Glob pattern to exclude (repeatable). |
| `--dry-run` | Print changes to console instead of writing a patch. |
| `--output-safe-patch` | File path to write the safe changes patch. |
| `--output-review-patch` | File path to write ambiguous changes for review. |

## Applying Changes

Once you have reviewed the generated patch files, apply them using git:

```powershell
git apply safe.patch
```

## Documentation

For more architectural details, see [design.md](docs/design.md).
