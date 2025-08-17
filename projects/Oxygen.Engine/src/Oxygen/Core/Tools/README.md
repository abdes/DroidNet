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

## Directory Structure

```text
Tools/
├── CMakeLists.txt              # CMake configuration for tools
├── README.md                   # This file
└── BindlessCodeGen/            # Bindless constants generator
    ├── CMakeLists.txt
    ├── pyproject.toml          # Python package configuration
    ├── requirements.txt        # Python dependencies
    ├── src/bindless_codegen/   # Generator source code
    │   ├── __init__.py
    │   ├── _version.py
    │   ├── cli.py              # Command-line interface
    │   └── generator.py        # Core generation logic
    └── tests/                  # Unit tests
        └── test_generator_basic.py
```

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
cmake --build --preset=windows-debug --target generate_bindless_headers

# Or run directly (requires manual setup):
python -m bindless_codegen.cli \
  --input src/Oxygen/Core/Bindless/BindingSlots.yaml \
  --out-cpp src/Oxygen/Core/Bindless/BindingSlots.h \
  --out-hlsl src/Oxygen/Core/Bindless/BindingSlots.hlsl
```

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
