# BindlessCodeGen Tool

A Python package for generating C++ and HLSL header files that define bindless
rendering constants from a single YAML source-of-truth.

## Purpose

The BindlessCodeGen tool ensures consistency between CPU and GPU code when
accessing bindless resources by generating matching constant definitions from a
centralized YAML specification.

## What It Generates

From `src/Oxygen/Core/Meta/Bindless.yaml`, the tool generates:

- C++: `Generated.BindlessAbi.h` — domain constants and invalid index sentinel
- HLSL: `Generated.BindlessAbi.hlsl` — matching shader constants and helpers
- C++: `Generated.RootSignature.D3D12.h` — root param indices, constants counts,
  register/space hints
- C++: `Generated.Meta.h` — compile-time meta constants (source path, versions,
  timestamp)
- JSON: `Generated.All.json` — machine-friendly normalized descriptor of the
  spec
- JSON: `Generated.Strategy.D3D12.json` — D3D12 strategy JSON with metadata
- JSON: `Generated.Strategy.Vulkan.json` — Vulkan strategy JSON with metadata
- C++: `Generated.Strategy.D3D12.h` — constexpr-embedded copy of the D3D12
  strategy JSON

### Example

**YAML Input:**

```yaml
abi:
  index_spaces:
    - id: srv_uav_cbv
  domains:
    - id: textures
      name: "Textures"
      index_space: srv_uav_cbv
      shader_index_base: 5096
      capacity: 65536
      shader_access_class: texture_srv
      view_types: [Texture_SRV]
      comment: "Texture SRV array"
```

**Generated C++ Output (Constants):**

```cpp
namespace oxygen::bindless::generated {
  inline constexpr uint32_t kTexturesShaderIndexBase = 5096u;
  static constexpr uint32_t kTexturesCapacity = 65536u;
}
```

**Generated HLSL Output:**

```hlsl
static const uint K_TEXTURES_SHADER_INDEX_BASE = 5096;
static const uint K_TEXTURES_CAPACITY = 65536;
```

**Generated Meta Header:**

```cpp
// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 2.0.0
// Schema-Version: 2.0.0
// Tool: BindlessCodeGen 1.2.2
// Generated: 2025-08-17 20:52:23

namespace oxygen::bindless::generated {
  static constexpr const char kBindlessSourcePath[] = "projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml";
  static constexpr const char kBindlessSourceVersion[] = "2.0.0";
  static constexpr const char kBindlessSchemaVersion[] = "2.0.0";
  static constexpr const char kBindlessToolVersion[] = "1.2.2";
  static constexpr const char kBindlessGeneratedAt[] = "2025-08-17 20:52:23";
}
```

## Key Locations

- **Tool Package**: `src/Oxygen/Core/Tools/BindlessCodeGen/src/bindless_codegen`
- **YAML Source**: `src/Oxygen/Core/Meta/Bindless.yaml`
- **Generated C++**: `src/Oxygen/Core/Bindless/Generated.BindlessAbi.h`,
  `Generated.RootSignature.D3D12.h`, `Generated.PipelineLayout.Vulkan.h`,
  `Generated.Meta.h`, `Generated.Strategy.D3D12.h`
- **Generated HLSL**: `src/Oxygen/Core/Bindless/Generated.BindlessAbi.hlsl`
- **Generated JSON**: `src/Oxygen/Core/Meta/Generated.All.json`,
  `Generated.Strategy.D3D12.json`, `Generated.Strategy.Vulkan.json`

## Usage

### Command Line Interface

Run the packaged CLI from the repository root:

```powershell
python -m bindless_codegen.cli `
  --input src/Oxygen/Core/Meta/Bindless.yaml `
  --out-base src/Oxygen/Core/Bindless/Generated.
```

Notes:

- Use `--out-base` to emit the full set in one go (BindlessAbi.h,
  BindlessAbi.hlsl, RootSignature.D3D12.h, PipelineLayout.Vulkan.h, Meta.h,
  All.json, Strategy.D3D12.json/.h, Strategy.Vulkan.json).
- Flags: `-v/--verbose` for more logs, `-q/--quiet`,
  `--color=auto|always|never`.

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

**Generate Bindless Outputs (Recommended):**

```powershell
cmake --build --preset=windows-debug --target oxygen-core_bindless_gen
```

This target:

- Depends on `bindless_codegen_editable_install`
- Re-runs only when the YAML source or tool code changes
- Generates the full output set (C++/HLSL/JSON), including `Generated.Meta.h`
- Integrates with the build dependency graph

### Compile-check (header validation)

When you build the `oxygen-core_bindless_gen` target, the tool also runs a
small compile-only check that includes the generated headers (kept under the
tool). This ensures the generated C++ headers remain syntactically and
semantically valid for the engine build. The compile-check is implemented as
an OBJECT target (`BindlessCodeGen_CompileCheck`) under the BindlessCodeGen
tool and will fail the generation step if compilation of the generated
headers fails.

If you prefer to run generation without running the compile-check (for a
one-off quick generation), build only the generated files by
invoking the Python CLI directly rather than the CMake target, or adjust CMake
configuration in environments where the check should be skipped.

Example (build generation and compile-check via CMake):

```powershell
cmake --build --preset=windows-debug --target oxygen-core_bindless_gen
```

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
├── CMakeLists.txt               # CMake configuration
├── pyproject.toml               # Python package configuration
├── requirements.txt             # Python dependencies
├── README.md                    # This file
├── examples/                    # Sample specs/usages (if any)
├── src/
│   └── bindless_codegen/        # Generator source code
│       ├── __init__.py
│       ├── _version.py          # Tool version
│       ├── cli.py               # Command-line interface
│       ├── generator.py         # Orchestrates parsing/validation/rendering
│       ├── reporting.py         # Color/verbosity-aware Reporter
│       ├── templates.py         # C++/HLSL/JSON templates
│       ├── heaps.py             # D3D12 heaps strategy builder
│       ├── domains.py           # Domain validation/helpers
│       ├── root_signature.py    # Root signature codegen
│       ├── model.py             # Datamodel for spec/meta
│       └── schema.py            # Schema loading/validation utilities
└── tests/                       # Unit tests
  ├── test_generator_basic.py
  ├── test_cli_dry_run.py
  ├── test_bindless_validations.py
  ├── test_validation.py
  └── test_validation_improvements.md
```

### Running Tests

**Via CTest (Recommended):**

```powershell
# Run all tests including BindlessCodeGen:
cmake --build --preset=windows-debug
ctest --preset=test-windows -C Debug --output-on-failure

# Run only BindlessCodeGen tests:
ctest --preset=test-windows -C Debug -R BindlessCodeGen_UnitTests --output-on-failure

# Run only YAML examples validation (CTest target: BindlessExamplesValidate):
ctest --preset=test-windows -C Debug -R BindlessExamplesValidate --output-on-failure

# Run tests with specific labels:
ctest --preset=test-windows -C Debug -L "Tools" --output-on-failure
```

The examples validation test runs the Python script at
`src/Oxygen/Core/Tools/BindlessCodeGen/examples/run_validate_examples.py` to
validate example YAML specs. If the editable install target exists, it’s set as
an explicit dependency so the test can import the tool without extra steps.

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

## YAML Schema and Heaps JSON

- Schema version is declared inside the schema file as `x-oxygen-schema-version`
  and is emitted into all generated files.
- The `Bindless.yaml` file uses the following structure:

```yaml
meta:
  version: "2.0.0"
  description: "Description of the bindless ABI"
defaults:
  invalid_index: 4294967295
abi:
  index_spaces:
    - id: srv_uav_cbv
    - id: sampler
  domains:
    - id: domain_id
      name: "DisplayName"
      index_space: srv_uav_cbv
      shader_index_base: 1
      capacity: 1000
      shader_access_class: buffer_srv|buffer_uav|texture_srv|texture_uav|constant_buffer|sampler|ray_tracing_accel_structure_srv
      view_types: [StructuredBuffer_SRV]
      comment: "Description"
backends:
  d3d12:
    strategy:
      heaps: [...]
      tables: [...]
      domain_realizations: [...]
    root_signature:
      - type: descriptor_table
        id: GlobalSRVTable
        table: GlobalSRVTable
        index: 0
        visibility: ALL
      - type: cbv
        id: SceneConstants
        index: 1
        shader_register: b1
        register_space: space0
        visibility: ALL
  vulkan:
    strategy:
      descriptor_sets: [...]
      bindings: [...]
      domain_realizations: [...]
    pipeline_layout:
      - type: descriptor_set
        id: BindlessMain
        set_ref: bindless_main
```

Notes:

- ABI domain overlap is validated per `index_space`, not globally across
  unrelated spaces such as SRV/UAV/CBV versus SAMPLER.
- Root CBVs and root constants are backend realization data, not bindless
  domains.

### D3D12 Strategy JSON

- Emitted as `Generated.Strategy.D3D12.json` and embedded in
  `Generated.Strategy.D3D12.h`.
- Structure:

```json
{
  "$meta": {
    "source": "projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml",
    "source_version": "1.0.0",
    "schema_version": "1.0.0",
    "tool_version": "1.0.0",
    "generated": "YYYY-MM-DD HH:MM:SS",
    "format": "BindlessStrategy.D3D12/1"
  },
  "heaps": {
  "CBV_SRV_UAV:cpu": { "capacity": 1000000, ... },
  "CBV_SRV_UAV:gpu": { "capacity": 1000000, ... },
    "SAMPLER:cpu": { ... },
    "SAMPLER:gpu": { ... },
    "RTV:cpu": { ... },
    "DSV:cpu": { ... }
  }
}
```

Use `oxygen::bindless::generated::d3d12::kStrategyJson` to parse at runtime.

## References

- **Bindless Rendering Design**: See `design/BindlessRenderingDesign.md` for
  architectural details
- **Root Signature**: See `design/BindlessRenderingRootSignature.md` for binding
  conventions
- **Pipeline Design**: See `design/PipelineDesign.md` for PSO integration

For questions about the BindlessCodeGen tool or to report issues, contact the
Graphics/Core maintainers listed in the repository `AUTHORS` file.
