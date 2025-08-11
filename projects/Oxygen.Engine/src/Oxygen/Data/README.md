# Oxygen Engine Data Module – Developer Onboarding Guide

This guide gives you everything you need to become productive inside the
`src/Oxygen/Data` module: architecture, core types, build & test workflow,
extension points, and common pitfalls.

---

## 1. Purpose & Scope

The Data module provides the immutable runtime representations of engine
assets and low-level resources that originate from offline content (PAK files)
or are generated procedurally at runtime. It covers:

* Asset metadata base (`Asset`) + concrete assets (`GeometryAsset`, `MaterialAsset`).
* Resource wrappers that are NOT first-class assets (`BufferResource`, `TextureResource`, `ShaderReference`).
* Geometry composition hierarchy (`Mesh`, `SubMesh`, `MeshView`) plus the
  `MeshBuilder` / `SubMeshBuilder` fluent construction API.
* Procedural primitive generation utilities (`ProceduralMeshes.*` & individual
  shape files under `Procedural/`).
* Shared value and enum types: `Vertex`, `AssetKey`, `AssetType`, `MeshType`,
  `MaterialDomain`, string conversion helpers.
* PAK binary layout specification (`PakFormat.h`).

Everything is designed to be:

* **Immutable after construction** – safe concurrent reads, easier reasoning.
* **Explicit & validated early** – invariants enforced via debug checks.
* **Shareable** – `std::shared_ptr` for lifetime across systems.
* **Zero‑copy viewing** – `MeshView` slices without data duplication.

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

| Type | Responsibility | Notes |
|------|----------------|-------|
| `Asset` | Base class for asset metadata | Provides type, name, version, hash, variant flags |
| `AssetKey` | 128‑bit GUID | Stable identity; hash + to_string |
| `GeometryAsset` | Group of LOD meshes + bounds | Holds `std::vector<std::shared_ptr<Mesh>>` |
| `Mesh` | Vertex/index storage + submeshes + bounds | Storage variant: owned vs referenced |
| `SubMesh` | Material + ≥1 `MeshView` | Enforces 1:1 material & ≥1 view |
| `MeshView` | Non-owning slice of mesh data | Ranges validated on creation |
| `MeshBuilder` | Fluent safe construction | Prevents mixed storage modes |
| `MaterialAsset` | PBR parameters + texture indices + shader refs | Factories: `CreateDefault`, `CreateDebug` |
| `ShaderReference` | Stage + unique id + source hash | Inline after material desc |
| `BufferResource` | Raw / typed / structured buffer descriptor + bytes | Usage flags + format interpretation |
| `TextureResource` | Texture descriptor + bytes | Width/height/depth/mips/format |
| `Vertex` | Position/normal/UV/tangent frame/color | Epsilon equality + quantized hash |

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

---

## 5. Invariants & Validation (Read First)

| Invariant | Enforced Where |
|-----------|----------------|
| Mesh has ≥1 vertex (owned storage) | Mesh ctor (CHECK_F) |
| Mesh has ≥1 submesh | `MeshBuilder::Build` |
| SubMesh has ≥1 MeshView | Builder + death tests |
| SubMesh has exactly 1 material (non-null) | SubMesh ctor |
| MeshView ranges in-bounds & counts > 0 | MeshView ctor |
| Single storage mode per MeshBuilder | `ValidateStorageType` (logic_error) |
| Index buffer element count aligns with size | Referenced storage init |
| Referenced mesh may omit index buffer (vertex-only allowed) | Builder + tests (IndexCount==0 path) |
| Owned-storage mesh must provide indices when vertices supplied (enforced via tests) | MeshBuilder death tests |
| Procedural params in valid range | Each generator (returns std::nullopt) |

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

Preferred workflow: use CMake presets (they encapsulate cache variables,
toolchain, Conan integration, and multi-config build logic). Manual commands
are still documented as a fallback or for CI scripting.

### 7.1 Preset-Based Build (Recommended)

First run Conan to materialize dependencies and generate the Conan CMake
toolchain + included preset snippet (required for both preset and manual
workflows):

```powershell
conan install . --profile:host=profiles/windows-msvc.ini --profile:build=profiles/windows-msvc.ini --output-folder=out/build --build=missing --deployer=full_deploy -s build_type=Debug
```

Then configure (creates build dir, consumes Conan toolchain, sets cache vars):

```powershell
cmake --preset windows -DOXYGEN_BUILD_TESTS=ON
```

Build (Debug):

```powershell
cmake --build --preset windows-debug --target oxygen-data Oxygen.Data.All.Tests Oxygen.Data.LinkTest
```

Build (Release):

```powershell
cmake --build --preset windows-release --target oxygen-data
```

Run tests with preset (Debug config implied by build preset):

```powershell
ctest --preset test-windows -R Oxygen.Data --output-on-failure
```

Notes:

* `windows` configure preset inherits `conan-default` (see `tools/presets/*`).
* Build presets choose configuration: `windows-debug` / `windows-release`.
* You can still pass extra cache vars: `cmake --preset windows -DOXYGEN_BUILD_TESTS=OFF`.

### 7.2 Manual (Fallback) Build

Only use if you must override preset logic explicitly (e.g., experimental
toolchain path). Equivalent to the preset steps above:

```powershell
conan install . --profile:host=profiles/windows-msvc-asan.ini --profile:build=profiles/windows-msvc-asan.ini --output-folder=out/build --build=missing --deployer=full_deploy -s build_type=Debug
cmake -S . -B out/build -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=out/build/conan_toolchain.cmake -DOXYGEN_BUILD_TESTS=ON
cmake --build out/build --target oxygen-data Oxygen.Data.All.Tests Oxygen.Data.LinkTest --config Debug
```

Target names (from CMake):

* Library: `oxygen-data` (alias `oxygen::data`).
* Link test exe: `Oxygen.Data.LinkTest`.
* Mesh tests exe: `Oxygen.Data.All.Tests` (aggregated GTest sources).
* Historical/alt aggregated name in docs/code examples: `Mesh_tests` (older helper macro output).

---

## 8. Running Tests

After building with `-DOXYGEN_BUILD_TESTS=ON`:

```powershell
ctest --test-dir out/build --output-on-failure -C Debug -R Oxygen.Data
# Or run specific target executable directly
out/build/bin/Debug/Oxygen.Data.LinkTest.exe
out/build/bin/Debug/Oxygen.Data.All.Tests.exe --gtest_filter=MeshAssetBasicTest.*
```

Filtering examples (new exe name):

```powershell
out/build/bin/Debug/Oxygen.Data.All.Tests.exe --gtest_filter=ProceduralMeshTest.MeshValidity
```

---

## 9. Writing New Tests

Follow rules in `.github/instructions/unit_tests.instructions.md`:

* Include `#include <Oxygen/Testing/GTest.h>` (wrapper) only.
* Use scenario-based names: `MeshBuilderErrorTest_WithVerticesAfterBuffers_Throws`.
* Use AAA structure with comments (Arrange / Act / Assert) & blank lines.
* Place tests in `src/Oxygen/Data/Test` – add file to `Test/CMakeLists.txt` if
  not using existing GTest macro (or extend `m_gtest_program` list).
* For death tests use `EXPECT_DEATH` and keep regex minimal.
* Use provided matchers (`SizeIs`, `AllOf`, etc.) and helper macros (`NOLINT_TEST`, `NOLINT_TEST_F`).
* Validate both positive and negative paths; prefer consolidating closely
  related error cases into one parameterized-style loop if possible.
* When asserting on thrown logic_error, check diagnostic substring quality.

Edge cases worth testing when adding features:

* Empty / malformed descriptors rejected at construction.
* Mixed storage mode misuse (already covered—mirror style if adding new mode).
* Bounding box/sphere correctness for new vertex attribute variations.
* Serialization / versioning changes (add regression tests once loader exists).

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

* Extend `MeshType` enum + `to_string`.
* Add new union branch in mesh descriptor (Pak format) & adapt `MeshBuilder`.
* Provide specialized validation (e.g., bone weights sum ≈ 1).

Performance tweaks (safe & optional):

* Reserve capacities in generators (e.g., plane, sphere) to reduce reallocs.
* Cache default/debug materials (singleton) if profiling shows allocation hot spots.

---

## 11. Troubleshooting

| Symptom | Likely Cause | Action |
|---------|--------------|--------|
| Crash inside MeshView ctor | Out-of-range descriptor values | Validate source descriptor before construction |
| Empty index buffer unexpectedly | Index format/stride mismatch or size misaligned | Inspect `BufferResource` element_format & stride; logging will warn |
| Logic error mixing storage | Called `WithBufferResources` after `WithVertices` (or vice versa) | Use one storage mode per builder instance |
| Bounding sphere seems large | Sphere derived from AABB, not minimal | Implement tighter sphere if necessary |
| Procedural generator returns nullopt | Invalid parameters (below minimums) | Re-check segment/size constraints |

---

## 12. Future Work (Planned Directions)

* Skinned / morph target mesh descriptors.
* Audio & animation asset wrappers.
* Optional cached materials & procedural mesh registry introspection.
* Tight bounding sphere computation (Ritter / Exact) if needed for culling.
* Loader integration tests once full asset streaming system lands.

---

## 13. Quick Reference Commands

Configure + build (preset example):

```powershell
conan install . --profile:host=profiles/windows-msvc.ini --profile:build=profiles/windows-msvc.ini --output-folder=out/build --build=missing --deployer=full_deploy -s build_type=Release
cmake --preset windows -DOXYGEN_BUILD_TESTS=ON
cmake --build --preset windows-release --target oxygen-data
```

Manual fallback (Release):

```powershell
conan install . --profile:host=profiles/windows-msvc.ini --profile:build=profiles/windows-msvc.ini --output-folder=out/build --build=missing --deployer=full_deploy -s build_type=Release
cmake -S . -B out/build -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=out/build/conan_toolchain.cmake -DOXYGEN_BUILD_TESTS=ON
cmake --build out/build --target oxygen-data --config Release
```

Test subset (preset):

```powershell
ctest --preset test-windows -R MeshAssetBasicTest --output-on-failure
```

Run single executable (Debug preset already built):

```powershell
out/build/bin/Debug/Oxygen.Data.All.Tests.exe --gtest_filter=MeshBuilderBasicTest.*
```

---

## 14. Guiding Principles Recap

* Validate *early*, fail *loudly* in debug.
* Keep runtime objects lean & immutable.
* Separate raw resource ownership from logical views.
* Prefer composition (builder + views) over inheritance for geometry.
* Make extension points obvious (enums + descriptors + builders).

Welcome aboard—start by reading `PakFormat.h`, then skim `GeometryAsset.h`
and `MeshBuilder` usage in tests to cement the mental model.
