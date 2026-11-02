# RenderDoc External Dependency

This directory intentionally vendors exactly one upstream RenderDoc file:

- `renderdoc_app.h`

Oxygen uses that header for the optional D3D12 `FrameCaptureController`
integration. The runtime RenderDoc binaries are not vendored in this repository.
Developers are expected to install RenderDoc separately and choose one of the
supported runtime initialization modes:

- attached only
- DLL search path
- explicit DLL path

Use RenderDoc v1.47 or later. Earlier releases crash when loading captures
with count-buffer `ExecuteIndirect` calls, which Vortex issues every frame
([#30](https://github.com/abdes/DroidNet/issues/30)). The build copies the
installed `renderdoc.dll` next to its binaries whenever that DLL changes, so
capture and replay use the same RenderDoc build.

## Acquisition

Use the repo-root fetch helper:

- `GetRenderDoc.ps1`
- `GetRenderDoc.bat`

Default installation target:

- `src/Oxygen/Graphics/External/RenderDoc/renderdoc_app.h`

Default pinned upstream release:

- `v1.47`

Latest-release fetch is supported via:

- `GetRenderDoc.ps1 -Latest`

## Upstream

- Repository: <https://github.com/baldurk/renderdoc>
- Header source: <https://raw.githubusercontent.com/baldurk/renderdoc/v1.47/renderdoc/api/app/renderdoc_app.h>

## Footprint Policy

Oxygen keeps the RenderDoc footprint intentionally minimal:

- vendor the single API header only
- dynamically load `renderdoc.dll` at runtime when configured
- do not static-link RenderDoc import libraries
- do not vendor `renderdoc.dll` or `qrenderdoc.exe`

## License

The vendored `renderdoc_app.h` carries the upstream MIT license notice and
should remain intact when updating the file.
