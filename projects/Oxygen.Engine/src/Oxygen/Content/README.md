# Oxygen Content System

> Canonical feature status & index for the Content (PAK) subsystem. Individual deep-dive docs are under `Docs/` and intentionally avoid duplicating the status tables below.

## Non-Negotiable Runtime Invariants (AssetLoader)

This section is normative. "MUST" and "MUST NOT" are strict requirements.

1. Single owning-thread execution for `AssetLoader` public API:
   `AssetLoader` public methods MUST run on the owning thread only. Ownership is bound on `ActivateAsync`.
   References: `AssetLoader.h:995-1003`, `AssetLoader.cpp:262-275`.

2. Async preconditions:
   Any async load path MUST fail fast if `nursery_` is not active or `thread_pool_` is null.
   References: `AssetLoader.cpp:1727-1734`, `AssetLoader.cpp:3115-3122`, `AssetLoader.cpp:987-995`.

3. Decode/publish split:
   Worker decode MUST be pure decode plus identity collection only.
   Owning-thread publish MUST perform cache insertion, dependency-edge publication, and refcount mutations.
   References: `AssetLoader.cpp:1721-1835`, `DependencyCollector.h:18-25`.

4. Identity-only dependency handoff:
   `DependencyCollector` MUST contain only `AssetKey`, `ResourceKey`, `ResourceRef`.
   It MUST NOT store paths, locators, readers, or stream handles.
   References: `DependencyCollector.h:18-25`, `AssetLoader.h:935-939`.

5. Source-aware identity:
   Asset and resource cache identity MUST be source-aware through `SourceKey` semantics, not mount order.
   References: `AssetLoader.cpp:4203-4215`, `AssetLoader.cpp:4217-4247`.

6. Resource key construction boundary:
   `ResourceKey` packing MUST remain an `AssetLoader`-internal concern.
   Decode code MUST use `ResourceRef` + `SourceToken` and bind on owning thread.
   References: `AssetLoader.h:1049-1056`, `ResourceRef.h:34-39`, `AssetLoader.cpp:743-757`.

7. Deterministic mount resolution:
   Source lookup for assets MUST preserve deterministic precedence (newest mount wins unless explicitly overridden).
   References: `AssetLoader.cpp:1782-1787`, per-type `resolve_source_id` lambdas.

8. Baseline cache retention model:
   After first store, loader keeps one baseline retain (`Touch`); dependency edges add retains; releases and trim remove retains symmetrically.
   References: `AssetLoader.cpp:544-550`, `AssetLoader.cpp:2007-2012`, `AssetLoader.cpp:2257-2263`.

9. Dependency release order:
   Asset release MUST process resource dependencies before asset dependencies, then the asset itself.
   References: `AssetLoader.cpp:1591-1637`.

10. Mount invalidation correctness:
    Refreshing or clearing mounts MUST leave no stale dependency graph edges or stale hash mappings.
    References: `AssetLoader.cpp:323-364`, `AssetLoader.cpp:408-433`, `AssetLoader.cpp:492-528`.

11. Source capability parity:
    If a source claims script table/data availability, generic script resource loading MUST work for that source.
    References: `LooseCookedSource.h:161-222`, `AssetLoader.cpp:3443-3463`.

12. Debug-only structural guards:
    Graph cycle detection and recursive-release visit guards are diagnostics only and MAY remain debug-only.
    Release runtime assumes acyclicity is guaranteed by import/authoring validation and CI checks.
    References: `AssetLoader.cpp:4131-4156`, `AssetLoader.cpp:1569-1589`.

## Documentation Index

| Topic | File | Focus |
| ----- | ---- | ----- |
| Entity relationships & intra-PAK rule | `Docs/overview.md` | Conceptual model & dependency boundaries |
| PAK format, alignment, classification | `Docs/chunking.md` | File layout, alignment, resource tiers |
| Loader & planned async pipeline | `Docs/asset_loader.md` | Sync loader + future coroutine design |
| Dependency tracking & caching | `Docs/deps_and_cache.md` | Reference counting, unload design |

---

## Implementation Status & Roadmap

All status tables and roadmap details have moved to `Docs/implementation_plan.md`. This README stays lightweight for orientation.

## Roadmap

See `Docs/implementation_plan.md#roadmap-phases`.

## LoaderContext Architecture

`LoaderContext` is a small value-type bundle passed to every asset/resource
loader so it can decode the descriptor and register dependencies as they are
discovered.

The authoritative definition is in `LoaderContext.h`.

### Key fields

- `current_asset_key`: the asset currently being loaded (empty for resources)
- `source_token`: identity-safe handle to the mounted source (used for `internal::ResourceRef`)
- `desc_reader`: positioned at the start of the descriptor to decode
- `data_readers`: per-resource-type readers positioned at the start of each
  resource data region (do not use `desc_reader` for data regions)
- `work_offline`: CPU-only mode flag (no renderer/GPU side effects)
- `dependency_collector`: optional identity-only dependency handoff for async decode
- `source_pak`: the `PakFile` the descriptor originates from
- `source_content`: source-agnostic content view for auxiliary reads (for example, scripting tables)
- `parse_only`: skip dependency collection (tooling/unit tests)

### Loader function shape

Load functions are registered with `AssetLoader::RegisterLoader(...)` and are
called as:

```cpp
std::unique_ptr<T> LoadXxx(LoaderContext context);
```

### Dependency registration

Dependencies are registered inline during loading. The loader records forward
dependencies and increments cache reference counts so that a later
`AssetLoader::ReleaseAsset(...)` can safely cascade releases.

```cpp
// Asset dependency (e.g., geometry -> material)
if (!context.parse_only && context.dependency_collector) {
  if (material_key != data::AssetKey{}) {
    context.dependency_collector->AddAssetDependency(material_key);
  }
}

// Resource dependency (e.g., material -> texture)
if (!context.parse_only && context.dependency_collector) {
  if (texture_index != data::pak::kNoResourceIndex) {
    internal::ResourceRef ref {
      .source = context.source_token,
      .resource_type_id = data::TextureResource::ClassTypeId(),
      .resource_index = texture_index,
    };
    context.dependency_collector->AddResourceDependency(ref);
  }
}
```

Asset→asset cycle detection is a debug-only runtime diagnostic. Release/runtime
behavior assumes acyclic graphs are enforced upstream by import/authoring
validation and CI tests. Release order remains resources first, then asset
dependencies, then the asset itself.

## Overview

The PAK file system provides efficient asset packaging and loading for the
Oxygen Engine. This guide covers the development tools for creating and
analyzing PAK files.

## Tools

### PAK Generator (`generate_pak.py`)

Creates PAK files from YAML asset specifications.

```bash
# Generate PAK from YAML
python generate_pak.py assets.yaml output.pak

# Force overwrite existing PAK
python generate_pak.py assets.yaml output.pak --force
```

**YAML Format:**

```yaml
assets:
  - asset_key: "12345678-1234-5678-9abc-123456789012"
    asset_type: 1  # AssetType enum value
    buffers: [0]   # Reference to buffer resources
    textures: [0]  # Reference to texture resources

buffer_resources:
  - data_hex: "deadbeef..."
    element_stride: 4
    element_format: 5      # Format::kR16UInt
    usage_flags: 1         # BufferUsageFlags

texture_resources:
  - data_hex: "cafebabe..."
    width: 256
    height: 256
    format: 30             # Format::kRGBA8UNorm
    texture_type: 0        # TextureType::k2D
```

### PAK Dumper (`PakFileDumper`)

Analyzes and inspects PAK file contents.

```bash
# Basic analysis
./PakFileDumper game.pak

# Verbose mode with all details
./PakFileDumper game.pak --verbose

# Show resource data (buffer/texture blobs)
./PakFileDumper game.pak --show-data

# Show asset descriptors (metadata)
./PakFileDumper game.pak --hex-dump-assets

# Combined detailed analysis
./PakFileDumper game.pak --verbose --show-data --hex-dump-assets
```

**Options:**

- `--verbose`: Show detailed resource information
- `--show-data`: Hex-dump buffer/texture blob data
- `--hex-dump-assets`: Hex-dump asset descriptor metadata
- `--no-header/footer/directory/resources`: Skip specific sections
- `--max-data=N`: Limit hex dump to N bytes (default: 256)

## Data Types

### Asset vs Resource Data

- **Asset Descriptors**: Metadata describing how to interpret assets (shown with `--hex-dump-assets`)
- **Resource Data**: Raw buffer/texture binary content (shown with `--show-data`)

### Supported Resource Types

- **BufferResource**: Vertex buffers, index buffers, uniform buffers
- **TextureResource**: 2D textures, cubemaps, texture arrays

## Development Workflow

1. **Create YAML**: Define assets and resources in YAML format
2. **Generate PAK**: Use `generate_pak.py` to create binary PAK file
3. **Validate**: Use `PakFileDumper` to verify correct generation
4. **Debug**: Use verbose modes to inspect specific data issues

## Example Test Cases

- `Simple.yaml` → `Simple.pak`: Basic single-asset test
- `Empty.yaml` → `Empty.pak`: Empty PAK validation
- `ComplexGeometry.yaml` → `ComplexGeometry.pak`: Multi-resource test with buffers and textures

All test files are located in `src/Oxygen/Content/Test/TestData/`.

## Asset Architecture Design

### Embedded vs Referenced Hierarchy

The Oxygen asset system uses a hybrid approach that optimizes for both sharing and performance:

#### Embedded Components (Always Co-loaded)

```text
GeometryAsset
├── Mesh[] (embedded)
│   ├── SubMesh[] (embedded)
│   │   ├── MeshView[] (embedded)
│   │   └── material_asset_key (reference)
│   └── vertex_buffer_ref (resource reference)
│   └── index_buffer_ref (resource reference)
```

#### Referenced Components (Shareable)

```text
Scene → GeometryAsset (AssetKey reference)
SubMesh → MaterialAsset (AssetKey reference)
MaterialAsset → TextureResource (resource table index)
MaterialAsset → ShaderResource (resource table index)
Mesh → BufferResource (resource table index - vertex/index buffers)
AudioAsset → AudioResource (resource table index)
```

### Key Design Benefits

1. **Memory Efficiency**: Mesh/SubMesh/MeshView are embedded as contiguous binary data within GeometryAsset descriptors
2. **Resource Sharing**: All resources (buffers, textures, shaders, audio) are referenced by index, enabling deduplication
3. **Simplified Dependencies**: Only asset-to-asset and asset-to-resource references need tracking
4. **Cache Coherency**: Embedded hierarchies load in a single read operation

### Reference Counting Need

Reference counting and cache eviction are implemented for both assets and resources. Assets and resources are only unloaded when their reference count reaches zero and no dependents remain. This ensures safe sharing and unloading of MaterialAssets, GeometryAssets, and all resource types.
needs reference counting to safely unload materials without breaking geometry
that depends on them.
