# External Integrations

**Analysis Date:** 2026-04-03

## APIs & External Services

**Dependency distribution and source hosting:**
- GitHub - Use it for bootstrap downloads and fetched assets
  - SDK/Client: PowerShell `Invoke-WebRequest` / `Invoke-RestMethod` in `GetDXC.ps1`, `GetRenderDoc.ps1`, and `cmake/DoxGeneration.cmake`
  - Auth: Not required in tracked scripts
- GitHub Raw Content - Use it to fetch the checked-in RenderDoc app header into `src/Oxygen/Graphics/External/RenderDoc/renderdoc_app.h` via `GetRenderDoc.ps1`
  - SDK/Client: `Invoke-WebRequest`
  - Auth: Not required in tracked scripts

**Windows graphics tooling:**
- PIX / WinPixEventRuntime - Use it for GPU markers and frame capture support in `src/Oxygen/Graphics/Direct3D12/PixFrameCaptureController.cpp` and SDK discovery in `src/Oxygen/Graphics/Direct3D12/CMakeLists.txt`
  - SDK/Client: Native PIX headers/DLLs plus provisioning script `GetPIX.ps1`
  - Auth: None for runtime use; `GetPIX.ps1` queries public NuGet metadata
- RenderDoc - Use it for frame capture in `src/Oxygen/Graphics/Direct3D12/RenderDocFrameCaptureController.cpp`
  - SDK/Client: `renderdoc_app.h` vendored under `src/Oxygen/Graphics/External/RenderDoc/` and download helper `GetRenderDoc.ps1`
  - Auth: Not required in tracked scripts
- NVIDIA Nsight Aftermath - Use it for GPU crash dumps in `src/Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.cpp`
  - SDK/Client: Native Aftermath headers/libs/DLLs staged by `GetAftermath.ps1`
  - Auth: None in engine code; download URL in `GetAftermath.ps1` points at NVIDIA-hosted SDK content

**Toolchain/package services:**
- NuGet flat container API - Use it to discover and download `WinPixEventRuntime` in `GetPIX.ps1`
  - SDK/Client: `Invoke-RestMethod` + `Invoke-WebRequest`
  - Auth: Not required in tracked scripts
- Conan remotes - Use Conan package sources declared operationally in `README.md` and resolved through `conanfile.py`
  - SDK/Client: Conan CLI
  - Auth: Not encoded in tracked files
- Git - Use it for version stamping in `CMakeLists.txt` through `cmake/GetGitRevisionDescription.cmake`
  - SDK/Client: `find_package(Git)` from CMake
  - Auth: Inherited from local Git configuration, not tracked here

## Data Storage

**Databases:**
- None
  - Connection: Not applicable
  - Client: Not applicable

**File Storage:**
- Local filesystem only
  - Cooked content and manifests are produced beside YAML specs by `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt`
  - Shader compiler inputs/outputs live in the source/build tree under `src/Oxygen/Graphics/Direct3D12/` and `out/`
  - Runtime DLL deployment uses `out/install/<Config>/bin` as configured in `CMakeLists.txt` and `tools/cli/oxyrun.ps1`
  - GPU crash dump artifacts are written to `logs/aftermath` by `src/Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.cpp`

**Caching:**
- Optional compiler cache via `ccache` in `cmake/FasterBuild.cmake`
- Build/package caches live in generated folders such as `out/build-*`, `out/install/*`, and Conan’s external cache; no application-level runtime cache service is configured

## Authentication & Identity

**Auth Provider:**
- None
  - Implementation: The engine/tooling stack does not define user accounts, tokens, OAuth flows, or service identity code in tracked sources

## Monitoring & Observability

**Error Tracking:**
- NVIDIA Nsight Aftermath - GPU crash dump capture in `src/Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.cpp`
- RenderDoc and PIX - Manual frame capture/debug instrumentation in `src/Oxygen/Graphics/Direct3D12/RenderDocFrameCaptureController.cpp` and `src/Oxygen/Graphics/Direct3D12/PixFrameCaptureController.cpp`

**Logs:**
- Internal logging layer centered on `src/Oxygen/Base/Logging.h` with implementation support in `src/Oxygen/Base/Detail/loguru.cpp`
- Local log artifacts are stored in the repository root and `logs/` (for example `flicker.log` and `logs/aftermath`)

## CI/CD & Deployment

**Hosting:**
- Native local/desktop deployment
  - Binaries and runtime DLLs are staged into `out/install/<Config>/bin`

**CI Pipeline:**
- GitHub-oriented build profile exists in `profiles/windows-github.ini`
- No tracked CI workflow directory was detected under `.github/workflows/`; use `tools/generate-builds.ps1`, `tools/cli/oxybuild.ps1`, and `tools/run-test-exes.ps1` as the practical automation entrypoints

## Environment Configuration

**Required env vars:**
- No mandatory project-specific environment variables were detected in tracked files
- Optional process environment usage exists:
  - `PATH` is prepended with config-scoped runtime DLL directories by `tools/cli/oxyrun.ps1`
  - `TEMP` is used in the README Conan settings install example and by dependency/bootstrap flows such as `GetPIX.ps1`
  - `VSCODE_PID` is consulted by `conanfile.py` when inferring Ninja usage
  - `PIP_DISABLE_PIP_VERSION_CHECK` is set during editable installs in `src/Oxygen/Core/Tools/BindlessCodeGen/CMakeLists.txt` and `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt`

**Secrets location:**
- Not detected
- No tracked secret store, vault integration, or `.env` workflow was detected in the repository files that were inspected

## Webhooks & Callbacks

**Incoming:**
- None
- Native SDK callbacks are used instead of HTTP webhooks; examples include `OnGpuCrashDump`, `OnShaderDebugInfo`, `OnCrashDumpDescription`, and `OnResolveMarker` in `src/Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.cpp`

**Outgoing:**
- HTTP GET requests to public package/source endpoints:
  - GitHub releases API from `GetDXC.ps1`
  - GitHub releases API and raw file URLs from `GetRenderDoc.ps1`
  - NuGet flat container API from `GetPIX.ps1`
  - NVIDIA SDK download URL from `GetAftermath.ps1`
  - GitHub ZIP fetch for documentation styling from `cmake/DoxGeneration.cmake`

---

*Integration audit: 2026-04-03*
