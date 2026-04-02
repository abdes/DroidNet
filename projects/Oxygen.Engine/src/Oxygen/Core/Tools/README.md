# Oxygen Core Tools

This directory contains developer tools for the Oxygen Engine Core module. These
tools generate code and assets used throughout the engine to maintain
consistency and reduce manual maintenance overhead.

## Overview

The Tools directory provides:

- **BindlessCodeGen**: Python package for generating bindless rendering
  constants from YAML specifications
- **CMake Integration**: Build system targets for seamless code generation
  during development

## BindlessCodeGen Tool

The BindlessCodeGen tool generates C++ and HLSL header files that define
bindless rendering constants from a single YAML source-of-truth. This ensures
consistency between CPU and GPU code when accessing bindless resources.

**Key Features:**

- Generates matching C++ and HLSL constants from YAML
- Atomic file writes to reduce unnecessary rebuilds
- CMake integration with dependency tracking
- Comprehensive test suite

**Quick Start:**

```powershell
# Install and generate headers (recommended):
cmake --build --preset=windows-debug --target oxygen-core_bindless_gen

# Or run directly (requires manual setup):
python -m bindless_codegen.cli `
  --input src/Oxygen/Core/Meta/Bindless.yaml `
  --out-base src/Oxygen/Core/Bindless/Generated.
```

This produces versioned artifacts in the Core source tree:

- `src/Oxygen/Core/Bindless/Generated.BindlessAbi.h` (C++ constants)
- `src/Oxygen/Core/Bindless/Generated.BindlessAbi.hlsl` (HLSL layout/macros)
- `src/Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h` (C++ root signature helpers)
- `src/Oxygen/Core/Bindless/Generated.PipelineLayout.Vulkan.h` (C++ Vulkan pipeline-layout helpers)
- `src/Oxygen/Core/Bindless/Generated.Meta.h` (compile-time metadata)
- `src/Oxygen/Core/Meta/Generated.All.json` (normalized runtime descriptor)
- `src/Oxygen/Core/Meta/Generated.Strategy.D3D12.json` (if D3D12 strategy is defined)
- `src/Oxygen/Core/Meta/Generated.Strategy.Vulkan.json` (if Vulkan strategy is defined)
- `src/Oxygen/Core/Bindless/Generated.Strategy.D3D12.h` (constexpr D3D12 strategy JSON embed)

Schema location: the generator auto-discovers `Bindless.schema.json` next to the input, or uses the repository default at `src/Oxygen/Core/Meta/Bindless.schema.json`.

**📖 Full Documentation:** See
[`BindlessCodeGen/README.md`](BindlessCodeGen/README.md) for detailed usage,
configuration, and development information.

## Future Tools

The Tools directory is designed to accommodate additional code generation and
build tools as the engine grows. New tools should follow the established
patterns:

- Self-contained Python packages with proper `pyproject.toml`
- CMake integration for build system automation
- Comprehensive unit tests
- Clear documentation and usage examples

For questions about tools or to report issues, contact the Graphics/Core
maintainers listed in the repository `AUTHORS` file.
