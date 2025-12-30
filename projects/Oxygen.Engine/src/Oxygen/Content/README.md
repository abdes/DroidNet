# Oxygen Content System

> Canonical feature status & index for the Content (PAK) subsystem. Individual deep-dive docs are under `Docs/` and intentionally avoid duplicating the status tables below.

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
- `offline`: CPU-only mode flag (no renderer/GPU side effects)
- `dependency_collector`: optional identity-only dependency handoff for async decode
- `source_pak`: the `PakFile` the descriptor originates from
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

Cycle detection is enforced for asset→asset dependencies, and release is
ordered (resources first, then asset dependencies, then the asset itself).

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
