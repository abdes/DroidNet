# Direct3D12 Shaders

This folder contains the HLSL sources used by the Direct3D12 backend.

## Source Layout

Shader entrypoints live under:

- `Passes/**/<Name>.hlsl`

Shared includes live under:

- `**/*.hlsli`

## Build Outputs

Shaders are compiled via DXC by a CMake custom target.

- Output directory (default presets): `out/build/shaders/<CONFIG>/`
- Typical outputs per entrypoint:
  - Compiled bytecode: `*_vs.cso`, `*_ps.cso`, `*_cs.cso`
  - Debug symbols (Debug config): matching `*.pdb`

> Note: The workspace often excludes `out/` from Explorer/search. If you don’t see
> generated files in VS Code, enable “Show Excluded Files” or adjust `files.exclude`.

## Building Shaders (Debug)

From the repo root:

Using CMake presets:

```powershell
cmake --build --preset windows-debug --target oxygen-graphics-direct3d12_shaders
```

This builds all discovered entrypoints under `Passes/**` and emits `-Zi`/`-Fd`
PDBs in `out/build/shaders/Debug/`.

## Troubleshooting

- If you recently moved/renamed shaders, stale outputs can remain under
  `out/build/shaders/<CONFIG>/`. Deleting that folder and rebuilding is safe.
