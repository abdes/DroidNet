# VS Code with clangd

Open `Oxygen.Engine` as the VS Code workspace folder. Use CMake Tools and
vscode-clangd 0.6.0 or newer (command substitutions are required). Python 3 must be
available as `python` on Windows or `python3` on Linux/macOS. No custom extension
or extension installation is performed by this workspace.

The integration was inspected against CMake Tools 1.24.42, including its native
`cmake.postConfigureTask` setting and build-directory/build-type commands.

1. Select a Ninja configure preset and a build preset in CMake Tools.
2. Configure. The native `cmake.postConfigureTask` runs **Oxygen: prepare clangd
   databases**. Wait for that task to finish successfully.
3. Run **clangd: Restart language server**.
4. When switching build configuration or configure preset, restart clangd again.
   A new build tree must be configured/prepared first. Switching Debug/Release
   within an already prepared multi-config tree does not require reconfiguration.

The clangd startup argument resolves CMake Tools' selected build directory and
build type. Each tree contains `clangd/<configuration>/compile_commands.json`.
The helper splits Ninja Multi-Config's database by its configuration-specific
object outputs, preserves compiler commands verbatim, and rejects unrecognized
output layouts rather than guessing. Ordinary Ninja uses `CMAKE_BUILD_TYPE`.
Unchanged files retain their timestamps; replacements are atomic.

Oxygen presets select CMake 4.2's native `CMAKE_INTERMEDIATE_DIR_STRATEGY=SHORT`
to keep object and compiler-metadata paths short. Target names, final binary
locations, and install locations retain their existing names. Switching an
existing tree to this strategy causes a one-time recompilation; there is no need
to delete the old tree. The clangd helper supports both `FULL` and `SHORT` output
layouts. Parent projects embedding Oxygen retain control of their own strategy.

The shared `.clangd` contains only compiler adaptation settings. CMake no longer
rewrites it. No compilation database is copied to the source root, and configuring
another tree does not select it for the current editor window. Microsoft C/C++
IntelliSense is disabled in this workspace to avoid duplicate language services;
the extension can still supply debugging features.

Visual Studio trees belong to Visual Studio, not to this clangd workflow. Missing
or invalid databases are task errors: configure the selected Ninja tree or rerun
the preparation task. If configuration was run outside CMake Tools, run the same
task explicitly, or use:

```powershell
python .vscode/prepare_clangd.py --build-dir out/build-ninja
```

Opening the repository root is a different VS Code workspace and does not inherit
this folder's settings/tasks. A parent embedding Oxygen owns its own editor
configuration; Oxygen's CMake never edits it.
