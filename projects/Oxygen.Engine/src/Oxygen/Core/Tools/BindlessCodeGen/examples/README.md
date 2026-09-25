# Bindless ABI examples

Each YAML file is a complete schema-v2 specification with a backend-neutral `abi`
and explicit D3D12 and Vulkan realizations. These small examples describe resource
layouts; they do not create GPU resources or exercise a renderer.

| Example                          | What it demonstrates                                                                                  |
| -------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `basic_minimal.yaml`             | One texture domain plus a D3D12 scene root CBV and Vulkan uniform-buffer binding.                     |
| `bindless_basic.yaml`            | A finite texture ABI capacity with an unbounded D3D12 table and variable-count Vulkan binding.        |
| `cbv_array_example.yaml`         | Sixteen constant-buffer descriptors at D3D12 `b2` and a Vulkan uniform-buffer array.                  |
| `cbv_array_example_small.yaml`   | A smaller eight-element constant-buffer array at D3D12 `b1`.                                          |
| `heaps_valid.yaml`               | Separate texture/sampler heaps and independent ABI index spaces.                                      |
| `multi_domain_range.yaml`        | Disjoint material/texture ranges in one D3D12 SRV table, with separately typed Vulkan bindings.       |
| `multi_space_srvs.yaml`          | D3D12 `t0` in two register spaces; Vulkan bindings in two descriptor sets.                            |
| `root_constants_and_tables.yaml` | Four inline 32-bit root constants, a 16-byte Vulkan push-constant range, and a material table.        |
| `sampler_table.yaml`             | A dedicated sampler heap and a fixed sampler array.                                                   |
| `uav_example.yaml`               | A bounded array of writable structured-buffer descriptors.                                            |
| `uav_with_counter.yaml`          | Structured particle UAVs plus a separately addressable raw buffer for an application-managed counter. |

The v1 fields `cbv_array_size` and `uav_counter_register` are retired. CBV arrays
are descriptor tables, not arrays of root descriptors. The counter example uses
an explicit buffer that application/shader code must manage; it does not allocate
or associate a native D3D12 hidden UAV counter. The generator does not perform
resource allocation, descriptor writes, barriers or counter initialization.

Root CBVs and inline constants are backend layout entries, not ABI domains.
ABI shader-index ranges must be disjoint within an index space. D3D12 heap-local
ranges and Vulkan binding-local array ranges are validated independently; their
offsets need not equal the ABI shader-index base.

## Validate locally

Provision and activate the repository environment using the
[tool requirements](../README.md#requirements). From Oxygen.Engine:

```powershell
python src/Oxygen/Core/Tools/BindlessCodeGen/examples/run_validate_examples.py
# Optional generator progress:
python src/Oxygen/Core/Tools/BindlessCodeGen/examples/run_validate_examples.py -v
```

The script uses the provisioned package and the explicit
[current schema](../../../Meta/Bindless.schema.json). It validates every YAML file
through the generator's schema and semantic checks, reports all failures, and
returns nonzero if any example fails or if the schema/example set is missing.
Dry-run validation writes no generated artifacts. It does not skip unfamiliar
input based on its top-level keys.

## Validate through CTest

Both `BUILD_TESTING` and `OXYGEN_BUILD_TESTS` must allow Oxygen tests. After
provisioning and configuring the Ninja tree, run from Oxygen.Engine:

```powershell
ctest --preset oxygen-ninja-debug -R '^BindlessExamplesValidate$' --output-on-failure
```

Visual Studio trees use `oxygen-vs-debug` instead. CTest invokes the same script
with the configured interpreter; it does not install dependencies or build code.

## Generate an example

To inspect actual output without replacing the engine's generated headers:

```powershell
python -m bindless_codegen.cli --input src/Oxygen/Core/Tools/BindlessCodeGen/examples/bindless_basic.yaml --out-base out/bindless-example/Generated.
```

Emission requires clang-format. For the real engine ABI, edit
[Bindless.yaml](../../../Meta/Bindless.yaml) and use the normal generation target
as described in the [tool guide](../README.md#cmake-integration).

Keep future examples small and focused. Every YAML in this directory must be
valid; intentionally invalid fixtures belong in unit tests, where failure is
asserted explicitly.
