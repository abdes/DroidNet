# BindlessCodeGen Tool

A Python package for generating C++ and HLSL header files that define bindless
rendering constants from a single YAML source-of-truth.

## Purpose

The BindlessCodeGen tool ensures consistency between CPU and GPU code when
accessing bindless resources by generating matching constant definitions from a
centralized YAML specification.

## What It Generates

From `src/Oxygen/Core/Bindless/BindingSlots.yaml`, the tool generates:

- **C++ Header** (`BindingSlots.h`): Constants for bindless slot indices, domain
  bases, and capacities
- **HLSL Header** (`BindingSlots.hlsl`): Equivalent constants for shader code

### Example

**YAML Input:**

```yaml
domains:
  - id: textures
    name: "Textures"
    kind: srv
    domain_base: 5096
    capacity: 65536
    comment: "Texture SRV array"
```

**Generated C++ Output:**

```cpp
namespace oxygen::engine::binding {
    static constexpr uint32_t Textures_DomainBase = 5096u;
    static constexpr uint32_t Textures_Capacity = 65536u;
}
```

**Generated HLSL Output:**

```hlsl
static const uint TEXTURES_DOMAIN_BASE = 5096;
static const uint TEXTURES_CAPACITY = 65536;
```

## Key Locations

- **Tool Package**: `src/Oxygen/Core/Tools/BindlessCodeGen/src/bindless_codegen`
- **YAML Source**: `src/Oxygen/Core/Bindless/BindingSlots.yaml`
- **Generated C++**: `src/Oxygen/Core/Bindless/BindingSlots.h`
- **Generated HLSL**: `src/Oxygen/Core/Bindless/BindingSlots.hlsl`

## Usage

### Command Line Interface

Run the packaged CLI from the repository root:

```powershell
python -m bindless_codegen.cli \
  --input src/Oxygen/Core/Bindless/BindingSlots.yaml \
  --out-cpp src/Oxygen/Core/Bindless/BindingSlots.h \
  --out-hlsl src/Oxygen/Core/Bindless/BindingSlots.hlsl
```

### CMake Integration

The build system provides two CMake targets:

**Install the Tool (Editable):**

```powershell
cmake --build --preset=windows-debug --target bindless_codegen_editable_install
```

This target:

- Installs Python package dependencies from `requirements.txt`
- Performs an editable install (`pip install -e .`) of the BindlessCodeGen tool
- Makes the `bindless_codegen` module available to Python in your environment
- Creates a stamp file to avoid reinstalling when unchanged

**Generate Headers (Recommended for Development):**

```powershell
cmake --build --preset=windows-debug --target generate_bindless_headers
```

This target:

- Automatically depends on `bindless_codegen_editable_install`
- Runs the generator only when the YAML source or tool code changes
- Generates both C++ and HLSL headers from the YAML source-of-truth
- Integrates with the build system dependencies

## Requirements

- **Python**: 3.8 or newer
- **Dependencies**: PyYAML (specified in `requirements.txt`)

### Installation Options

1. **Via CMake (Recommended for Development):**

   ```powershell
   cmake --build --preset=windows-debug --target bindless_codegen_editable_install
   ```

2. **Manual Installation:**

   ```powershell
   pip install -r requirements.txt
   pip install -e .
   ```

## Development and Testing

### Package Structure

```text
BindlessCodeGen/
├── CMakeLists.txt          # CMake configuration
├── pyproject.toml          # Python package configuration
├── requirements.txt        # Python dependencies
├── README.md              # This file
├── src/bindless_codegen/  # Generator source code
│   ├── __init__.py
│   ├── _version.py
│   ├── cli.py             # Command-line interface
│   └── generator.py       # Core generation logic
└── tests/                 # Unit tests
    └── test_generator_basic.py
```

### Running Tests

**Via CTest (Recommended):**

```powershell
# Run all tests including BindlessCodeGen:
cmake --build --preset=windows-debug
ctest --preset=test-windows -C Debug --output-on-failure

# Run only BindlessCodeGen tests:
ctest --preset=test-windows -C Debug -R BindlessCodeGen_UnitTests --output-on-failure

# Run tests with specific labels:
ctest --preset=test-windows -C Debug -L "Tools" --output-on-failure
```

**Direct pytest:**

```powershell
# From the BindlessCodeGen directory with dependencies installed:
pytest tests/

# Or from the repository root:
pytest src/Oxygen/Core/Tools/BindlessCodeGen/tests/
```

The CTest integration automatically ensures the tool is installed before running
tests and integrates with the project's testing infrastructure.

### Editor Setup

If your editor reports unresolved imports for `bindless_codegen`, add the tool's
source directory to your Python analysis paths. The repository includes a
`pyrightconfig.json` that configures `extraPaths` for this purpose.

## Important Notes

- **Atomic Writes**: The generator writes files atomically and only updates
  outputs when content changes, reducing unnecessary rebuilds
- **Read-Only Outputs**: Never edit generated headers manually. Always update
  the YAML source and regenerate
- **Version Control**: Generated files are committed to the repository to ensure
  build consistency
- **Build Integration**: The CMake targets are designed to integrate seamlessly
  with the build system's dependency tracking

## YAML Schema

The `BindingSlots.yaml` file uses the following structure:

```yaml
binding_slots_version: 1
meta:
  description: "Description of the binding slots"
  source: "path/to/source.yaml"
defaults:
  invalid_index: 4294967295
domains:
  - id: domain_id           # Unique identifier
    name: "DisplayName"     # Human-readable name
    kind: srv|cbv|sampler   # Resource type
    register: t0|b0|s0      # HLSL register (optional)
    space: 0                # Register space (optional)
    domain_base: 0          # Base index in the domain
    capacity: 1000          # Maximum number of resources
    comment: "Description"  # Documentation
symbols:
  SymbolName:
    domain: domain_id       # Reference to domain
    comment: "Description"
  ConstantName:
    value: invalid_index    # Direct value assignment
    comment: "Description"
```

## References

- **Bindless Rendering Design**: See `design/BindlessRenderingDesign.md` for
  architectural details
- **Root Signature**: See `design/BindlessRenderingRootSignature.md` for binding
  conventions
- **Pipeline Design**: See `design/PipelineDesign.md` for PSO integration

For questions about the BindlessCodeGen tool or to report issues, contact the
Graphics/Core maintainers listed in the repository `AUTHORS` file.
