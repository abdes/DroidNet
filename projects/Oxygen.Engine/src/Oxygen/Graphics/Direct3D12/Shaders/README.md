# Direct3D12 Shaders

This folder contains the HLSL sources used by the Direct3D12 backend.

## Source Layout

Shader entrypoints live under:

- `Passes/**/<Name>.hlsl`

Shared includes live under:

- `**/*.hlsli`

## Build Outputs

Shaders are compiled through `ShaderBake`.

- Runtime-facing archive:
  - `bin/Oxygen/<build-config>/<mode>/shaders.bin`
- ShaderBake intermediary/cache state:
  - `out/build-*/shaderbake/state/`
  - `out/build-*/shaderbake/modules/`
- Developer-facing loose shader artifacts:
  - `bin/Oxygen/<build-config>/<mode>/dxil/<source-path>/<entry-point>__<request-key>.dxil`
  - `bin/Oxygen/<build-config>/<mode>/pdb/<source-path>/<entry-point>__<request-key>.pdb`

The loose DXIL/PDB files are intended for graphics debugging tools such as PIX,
RenderDoc, and Nsight. They are not runtime deployment inputs.

## Building Shaders (Debug)

From the repo root:

Using CMake presets:

```powershell
cmake --build --preset windows-debug --target oxygen-graphics-direct3d12
```

This updates the ShaderBake cache, publishes
`bin/Oxygen/Debug/dev/shaders.bin`, and emits loose DXIL/PDB sidecars beside it
under `bin/Oxygen/Debug/dev/dxil/` and `bin/Oxygen/Debug/dev/pdb/`.

## Troubleshooting

- If you recently moved/renamed shaders, stale outputs can remain under
  `out/build-*/shaderbake/`. Running `ShaderBake clean-cache` or deleting that
  folder and rebuilding is safe.
