# ShaderBake

`ShaderBake` is Oxygen.Engine's build-time Direct3D12 shader compiler and packer.
It expands the engine shader catalog, compiles concrete shader requests with DXC,
caches request-scoped intermediary artifacts under a build root, and emits one
runtime-facing OXSL archive at the requested output path.

The runtime contract is intentionally simple:

- Runtime loads one final shader library only.
- Runtime does not invoke DXC.
- Incremental correctness is owned by `ShaderBake`, not by the outer build
  generator.

## What ShaderBake Produces

For a given invocation, `ShaderBake` owns two output classes:

- Final runtime artifact:
  `shaders.bin` at the explicit `--out` path.
- Intermediary build state under `--build-root`:
  - `state/build-state-v1.json`
  - `state/manifest-v1.json`
  - `modules/<xx>/<yy>/<request-key>.oxsm`
  - `logs/<request-key>.log` in `dev` mode for failed requests
  - `temp/` for atomic publication staging
- Published developer-facing sidecars beside `shaders.bin`:
  - `dxil/<source-path>/<entry-point>__<request-key>.dxil`
  - `pdb/<source-path>/<entry-point>__<request-key>.pdb`

The final archive is written atomically and contains exactly the current engine
shader catalog membership.

## Build Model

`ShaderBake` implements a request-granular incremental pipeline:

1. Expand `kEngineShaders` into canonical `ShaderRequest` values.
2. Compute a stable request key for each concrete request.
3. Load prior `.oxsm` artifacts from `--build-root`.
4. Recompile only requests whose action identity, primary source, or resolved
   include dependencies changed.
5. Repack the final `shaders.bin` archive only when required.

Correctness does not rely on Ninja depfiles, Visual Studio tracking, or process
working directory.

## Commands

### `update`

Incremental build entry point.

- Expands the current engine shader catalog.
- Reuses clean `.oxsm` artifacts.
- Recompiles only dirty requests.
- Removes stale artifacts for requests no longer in the catalog.
- Recompiles a request if an expected sidecar PDB was removed.
- Repackages the final archive only if inputs or membership changed.

### `rebuild`

Clean rebuild entry point.

- Ignores prior module artifacts.
- Recompiles the full catalog.
- Rewrites build state after a successful run.
- Republishes the final archive atomically.

### `clean-cache`

Removes ShaderBake-owned intermediary state under `--build-root`.

- Deletes `state/`, `modules/`, `logs/`, and `temp/`.
- Does not delete `--out`.
- Does not delete published `dxil/` or `pdb/` sidecars beside `--out`.

### `inspect`

Reads and prints an OXSL shader library.

This is an archive inspection command. It does not compile shaders and it does
not inspect `.oxsm` module artifacts.

### `bake`

Legacy compatibility alias for `update`.

## CLI Reference

### Build Commands

`update`, `rebuild`, and `bake` accept:

- `--workspace-root <path>`: required repository/workspace root.
- `--build-root <path>`: required ShaderBake intermediary root.
- `--out <path>`: required final OXSL archive path.
- `--shader-root <path>`: optional shader source root.
  Default: `src/Oxygen/Graphics/Direct3D12/Shaders`
- `--oxygen-include-root <path>`: optional Oxygen include root.
  Default: `src/Oxygen`
- `--include-dir <path>`: optional extra include directory; repeatable.
- `--mode dev|production`: optional policy mode.
  Default: `dev`

`clean-cache` accepts the same path options. `--out` is optional for
`clean-cache` and is never deleted by the command.

Path rules:

- Relative paths are resolved against `--workspace-root`.
- `--build-root` owns all ShaderBake intermediary state.
- `--out` can be inside or outside the build tree.

### Inspect Command

`inspect` accepts:

- `--file <path>`: required OXSL archive path.
- `--header`
- `--modules`
- `--defines`
- `--offsets`
- `--reflection`
- `--all`

`--all` enables defines, offsets, and reflection details.

## Typical Usage

Incremental update:

```powershell
Oxygen.Graphics.Direct3D12.ShaderBake.exe update `
  --workspace-root <repo-root> `
  --build-root <build-dir>\shaderbake `
  --out <repo-root>\bin\Oxygen\Debug\dev\shaders.bin
```

Clean rebuild:

```powershell
Oxygen.Graphics.Direct3D12.ShaderBake.exe rebuild `
  --workspace-root <repo-root> `
  --build-root <build-dir>\shaderbake `
  --out <repo-root>\bin\Oxygen\Debug\dev\shaders.bin
```

Inspect a baked archive:

```powershell
Oxygen.Graphics.Direct3D12.ShaderBake.exe inspect `
  --file <repo-root>\bin\Oxygen\Debug\dev\shaders.bin `
  --header true `
  --modules true
```

Clear intermediates without touching the final archive:

```powershell
Oxygen.Graphics.Direct3D12.ShaderBake.exe clean-cache `
  --workspace-root <repo-root> `
  --build-root <build-dir>\shaderbake
```

## Build-System Integration

The engine's shader build integrates `ShaderBake` from
`src/Oxygen/Graphics/Direct3D12/Shaders/CMakeLists.txt` and currently invokes:

```text
ShaderBake update --workspace-root <repo-root> --build-root <build-dir>/shaderbake --out <repo-root>/bin/Oxygen/<build-config>/<mode>/shaders.bin
```

That split is intentional:

- `--build-root` is generator-local and disposable.
- `--out` is the runtime-facing archive location.
- Working directory is not part of ShaderBake correctness.

## Failure and Diagnostics

- A failed run never publishes a partial final archive.
- Module artifacts are written only after a request compiles successfully.
- Debug-capable builds publish loose DXIL and PDB sidecars beside `shaders.bin`.
- ShaderBake uses external DXC PDB output for those sidecars rather than embedding debug info in the compiled DXIL by default.
- In `dev` mode, failed requests write diagnostics to `logs/<request-key>.log`
  under `--build-root`.
- In `production` mode, per-request failure logs are not emitted.

`ShaderBake` returns:

- `0` on success.
- `2` for command-level operational failures.
- `1` for unhandled process-level failures.

## Prerequisites

`ShaderBake` depends on the DXC toolchain and runtime DLLs.

The repository expects the DXC package layout under:

- `packages/DXC/inc`
- `packages/DXC/lib/<arch>`
- `packages/DXC/bin/<arch>/dxcompiler.dll`
- `packages/DXC/bin/<arch>/dxil.dll`

The CMake target copies `dxcompiler.dll` and `dxil.dll` beside the executable
after build and fails configure early if the expected package files are missing.

## Related Documentation

- [ShaderBake vNext design](../../../../../../design/content-pipeline/shaderbake-vnext-design.md)
- [Direct3D12 shader layout](../../Shaders/README.md)
- [Vortex shader contracts](../../../../../../design/vortex/lld/shader-contracts.md)
