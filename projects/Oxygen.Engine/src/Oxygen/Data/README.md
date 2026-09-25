# Oxygen Engine Data Module – Developer Onboarding Guide

This guide gives you everything you need to become productive inside the
`src/Oxygen/Data` module: architecture, core types, build & test workflow,
extension points, and common pitfalls.

---

## 1. Purpose & Scope

The Data module provides the immutable runtime representations of engine
assets and low-level resources that originate from offline content (PAK files)
or are generated procedurally at runtime. It covers:

- Asset metadata base (`Asset`) + concrete assets (`GeometryAsset`, `MaterialAsset`).
- Resource wrappers that are NOT first-class assets (`BufferResource`, `TextureResource`, `ShaderReference`).
- Geometry composition hierarchy (`Mesh`, `SubMesh`, `MeshView`) plus the
  `MeshBuilder` / `SubMeshBuilder` fluent construction API.
- Procedural primitive generation utilities (`ProceduralMeshes.*` & individual
  shape files under `Procedural/`).
- Shared value and enum types: `Vertex`, `AssetKey`, `AssetType`, `MeshType`,
  `MaterialDomain`, string conversion helpers.
- PAK binary layout specification (`PakFormat.h`).

Everything is designed to be:

- **Immutable after construction** – safe concurrent reads, easier reasoning.
- **Explicit & validated early** – invariants enforced via debug checks.
- **Shareable** – `std::shared_ptr` for lifetime across systems.
- **Zero‑copy viewing** – `MeshView` slices without data duplication.

---

## 2. Architecture at a Glance

Layered model:

1. Binary Schema (PAK) – Packed POD structs (`PakFormat.h`).
2. Thin Runtime Wrappers – Mirror descriptors while adding typed accessors.
3. Geometry Composition – Owns / references raw buffers, exposes logical views.
4. Builders & Procedural Generators – Safe assembly + primitive creation.
5. Tests – Scenario-based invariants ensuring correctness & regression safety.

Relationships:
`GeometryAsset` → LOD vector of `Mesh` → vector of `SubMesh` → vector of `MeshView` → spans into vertex & index buffers.

Resources (`BufferResource`, `TextureResource`) are referenced by index from
asset descriptors; they are not listed in the asset directory themselves.

---

## 3. Core Types Cheat Sheet

| Type              | Responsibility                                     | Notes                                             |
| ----------------- | -------------------------------------------------- | ------------------------------------------------- |
| `Asset`           | Base class for asset metadata                      | Provides type, name, version, hash, variant flags |
| `AssetKey`        | 128‑bit GUID                                       | Stable identity; hash + to_string                 |
| `GeometryAsset`   | Group of LOD meshes + bounds                       | Holds `std::vector<std::shared_ptr<Mesh>>`        |
| `Mesh`            | Vertex/index storage + submeshes + bounds          | Storage variant: owned vs referenced              |
| `SubMesh`         | Material + ≥1 `MeshView`                           | Enforces 1:1 material & ≥1 view                   |
| `MeshView`        | Non-owning slice of mesh data                      | Ranges validated on creation                      |
| `MeshBuilder`     | Fluent safe construction                           | Prevents mixed storage modes                      |
| `MaterialAsset`   | PBR parameters + texture indices + shader refs     | Factories: `CreateDefault`, `CreateDebug`         |
| `ShaderReference` | Stage + unique id + source hash                    | Inline after material desc                        |
| `BufferResource`  | Raw / typed / structured buffer descriptor + bytes | Usage flags + format interpretation               |
| `TextureResource` | Texture descriptor + bytes                         | Width/height/depth/mips/format                    |
| `Vertex`          | Position/normal/UV/tangent frame/color             | Epsilon equality + quantized hash                 |

---

## 4. Geometry Data Flow

### From PAK (standard asset load)

1. Read directory entry (contains `AssetKey`, descriptor offsets).
2. Read `GeometryAssetDesc`, then mesh + submesh + view descriptors.
3. Create `BufferResource` objects for vertex & index tables.
4. Use `MeshBuilder.WithBufferResources(...)`, add SubMeshes & MeshViews.
5. Wrap meshes into a `GeometryAsset`.

### Procedural Generation

1. Call e.g. `MakeCubeMeshAsset()` → `(vertices, indices)` pair OR
   `GenerateMesh("Sphere/MySphere", param_bytes)` → ready `Mesh`.
2. Build mesh with `MeshBuilder.WithVertices/WithIndices`.
3. Attach default or debug material; add single full-range view.
4. (Optional) Promote to `GeometryAsset` if you need LOD semantics.

`MakeCapsuleMeshAsset()` and the `Capsule` generator share the same centred,
Z-axis recipe: total height 2 m and radius 0.5 m. Its parameters are
`hemisphere_segments` (default 8, range 1–64), `radial_segments` (default 32,
range 3–256), `height` and `radius`. Dimensions must be finite, positive and
representable by non-collapsed float32 vertex coordinates; height must be at
least the diameter. Equality produces a sphere without a duplicate equator.
The corresponding Physics capsule uses the same radius and a cylindrical
`half_height = height / 2 - radius`; that field excludes both hemispheres.
Both domains use Oxygen's Z axis. Backend shape conventions are adapted inside
Physics, before authored local transforms.

---

## 5. Invariants & Validation (Read First)

| Invariant                                                                           | Enforced Where                        |
| ----------------------------------------------------------------------------------- | ------------------------------------- |
| Mesh has ≥1 vertex (owned storage)                                                  | Mesh ctor (CHECK_F)                   |
| Mesh has ≥1 submesh                                                                 | `MeshBuilder::Build`                  |
| SubMesh has ≥1 MeshView                                                             | Builder + death tests                 |
| SubMesh has exactly 1 material (non-null)                                           | SubMesh ctor                          |
| MeshView ranges in-bounds & counts > 0                                              | MeshView ctor                         |
| Single storage mode per MeshBuilder                                                 | `ValidateStorageType` (logic_error)   |
| Index buffer element count aligns with size                                         | Referenced storage init               |
| Referenced mesh may omit index buffer (vertex-only allowed)                         | Builder + tests (IndexCount==0 path)  |
| Owned-storage mesh must provide indices when vertices supplied (enforced via tests) | MeshBuilder death tests               |
| Procedural params in valid range                                                    | Each generator (returns std::nullopt) |

Failing invariants: debug builds abort (tests assert). Production loaders must
ensure descriptors are sanitized before constructing objects.

---

## 6. Typical Code Snippets

Create procedural sphere mesh:

```cpp
auto data = oxygen::data::MakeSphereMeshAsset(16, 32);
if (!data) return; // invalid params
auto [verts, inds] = std::move(*data);
auto material = oxygen::data::MaterialAsset::CreateDefault();
auto mesh = oxygen::data::MeshBuilder(0, "SphereLOD0")
  .WithVertices(verts)
  .WithIndices(inds)
  .BeginSubMesh("full", material)
    .WithMeshView({ .first_index = 0,
                    .index_count = (uint32_t)inds.size(),
                    .first_vertex = 0,
                    .vertex_count = (uint32_t)verts.size() })
  .EndSubMesh()
  .Build();
```

Iterate indices agnostic of underlying 16/32-bit storage:

```cpp
auto view = mesh->SubMeshes()[0].MeshViews()[0];
for (uint32_t idx : view.IndexBuffer().Widened()) { /* ... */ }
```

---

## 7. Building This Module

Run from Oxygen.Engine in an initialized compiler shell. Data is part of the
full-engine build; it is not one of the six source-embeddable reusable modules.

```powershell
# One-time setup, or repeat when Conan/Python dependency inputs change:
.\tools\build-tree.ps1 generate profiles/windows-msvc.ini -Generator Ninja

# Reconfigure an existing tree after CMake edits:
.\tools\build-tree.ps1 configure oxygen-ninja-default

# Build just Data and its tests:
cmake --build --preset oxygen-ninja-debug --target oxygen-data Oxygen.Data.All.Tests Oxygen.Data.LinkTest
```

The contributor generation command enables tests. Both `BUILD_TESTING` and
`OXYGEN_BUILD_TESTS` must permit test creation. Use the ASan profile for the
separate ASan trees; see the [preset guide](../../../tools/presets/README.md).
For a Release library build, select `oxygen-ninja-release` instead.

Targets are `oxygen-data` (link alias `oxygen::data`), `Oxygen.Data.LinkTest`
and `Oxygen.Data.All.Tests`. Installed SDK and Conan applications link
`oxygen::data`; they do not build Oxygen's development tests.

---

## 8. Running Tests

After building the selected configuration:

```powershell
ctest --preset oxygen-ninja-debug -R '^Oxygen\.Data\.' --output-on-failure
# Select cases inside the aggregated executable through GoogleTest:
.\tools\cli\oxyrun.ps1 Oxygen.Data.All.Tests -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=MeshAssetBasicTest.*
```

CTest registers one entry per executable. A GoogleTest case name is not a CTest
name; use `--gtest_filter` for individual cases. The launcher supplies the selected
configuration's runtime paths without changing the caller's shell environment.

---

## 9. Writing New Tests

Follow rules in `.github/instructions/unit_tests.instructions.md`:

- Include `#include <Oxygen/Testing/GTest.h>` (wrapper) only.
- Use scenario-based names: `MeshBuilderErrorTest_WithVerticesAfterBuffers_Throws`.
- Use AAA structure with comments (Arrange / Act / Assert) & blank lines.
- Place tests in `src/Oxygen/Data/Test` – add file to `Test/CMakeLists.txt` if
  not using existing GTest macro (or extend `m_gtest_program` list).
- For death tests use `EXPECT_DEATH` and keep regex minimal.
- Use provided matchers (`SizeIs`, `AllOf`, etc.) and helper macros (`NOLINT_TEST`, `NOLINT_TEST_F`).
- Validate both positive and negative paths; prefer consolidating closely
  related error cases into one parameterized-style loop if possible.
- When asserting on thrown logic_error, check diagnostic substring quality.

Edge cases worth testing when adding features:

- Empty / malformed descriptors rejected at construction.
- Mixed storage mode misuse (already covered—mirror style if adding new mode).
- Bounding box/sphere correctness for new vertex attribute variations.
- Serialization / versioning changes (add regression tests once loader exists).

---

## 10. Extending the Module

Adding a new asset type:

1. Define enum entry in `AssetType` + update `to_string`.
2. Add packed descriptor struct to `PakFormat.h` (respect size & alignment; bump
   format version if layout changed globally).
3. Create wrapper class deriving from `Asset` with typed accessors.
4. Implement loader path (out of scope here) + tests (constructor correctness,
   invalid descriptor rejection, accessor integrity).

Adding a new procedural shape:

1. Add `<Shape>.cpp` under `Procedural/` modeled on existing ones.
2. Implement `Make<Shape>MeshAsset` returning `optional<pair<vector<Vertex>, vector<uint32_t>>>`.
3. Update dispatch in `ProceduralMeshes.cpp` (`InvokeGenerator` / parsing path).
4. Add tests: invalid params, minimal geometry counts, bounding box sanity.

Adding skinned / morph mesh support (future):

- Extend `MeshType` enum + `to_string`.
- Add new union branch in mesh descriptor (Pak format) & adapt `MeshBuilder`.
- Provide specialized validation (e.g., bone weights sum ≈ 1).

Performance tweaks (safe & optional):

- Reserve capacities in generators (e.g., plane, sphere) to reduce reallocs.
- Cache default/debug materials (singleton) if profiling shows allocation hot spots.

---

## 11. Troubleshooting

| Symptom                              | Likely Cause                                                      | Action                                                              |
| ------------------------------------ | ----------------------------------------------------------------- | ------------------------------------------------------------------- |
| Crash inside MeshView ctor           | Out-of-range descriptor values                                    | Validate source descriptor before construction                      |
| Empty index buffer unexpectedly      | Index format/stride mismatch or size misaligned                   | Inspect `BufferResource` element_format & stride; logging will warn |
| Logic error mixing storage           | Called `WithBufferResources` after `WithVertices` (or vice versa) | Use one storage mode per builder instance                           |
| Bounding sphere seems large          | Sphere derived from AABB, not minimal                             | Implement tighter sphere if necessary                               |
| Procedural generator returns nullopt | Invalid parameters (below minimums)                               | Re-check segment/size constraints                                   |

---

## 12. Future Work (Planned Directions)

- Skinned / morph target mesh descriptors.
- Audio & animation asset wrappers.
- Optional cached materials & procedural mesh registry introspection.
- Tight bounding sphere computation (Ritter / Exact) if needed for culling.
- Loader integration tests once full asset streaming system lands.

---

## 13. Quick Reference Commands

From an already configured Oxygen.Engine checkout:

```powershell
cmake --build --preset oxygen-ninja-release --target oxygen-data
cmake --build --preset oxygen-ninja-debug --target Oxygen.Data.All.Tests Oxygen.Data.LinkTest
ctest --preset oxygen-ninja-debug -R '^Oxygen\.Data\.' --output-on-failure
.\tools\cli\oxyrun.ps1 Oxygen.Data.All.Tests -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=MeshBuilderBasicTest.*
```

See [building](#7-building-this-module) for initial setup and
[running tests](#8-running-tests) for executable-level and case-level selection.

---

## 14. Guiding Principles Recap

- Validate _early_, fail _loudly_ in debug.
- Keep runtime objects lean & immutable.
- Separate raw resource ownership from logical views.
- Prefer composition (builder + views) over inheritance for geometry.
- Make extension points obvious (enums + descriptors + builders).

Welcome aboard—start by reading `PakFormat.h`, then skim `GeometryAsset.h`
and `MeshBuilder` usage in tests to cement the mental model.
