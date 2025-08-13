# Oxygen Content System

> Canonical feature status & index for the Content (PAK) subsystem. Individual deep-dive docs are under `Docs/` and intentionally avoid duplicating the status tables below.

## Documentation Index

| Topic | File | Focus |
|-------|------|-------|
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

### Unified Loading Interface

All asset and resource loaders now use a consistent LoaderContext interface:

```cpp
template<typename StreamT>
struct LoaderContext {
  AssetLoader* asset_loader;           // For dependency registration (nullable for resources)
  AssetKey current_asset_key;         // Asset being loaded
  std::reference_wrapper<Reader<StreamT>> reader;  // Stream reader
  bool offline;                       // Offline/GPU-less mode flag
};
```

### Key Benefits

- **Type Safety**: Template-based context ensures stream type consistency
- **Dependency Registration**: Dependencies registered at point of discovery during loading
- **Unified API**: All loaders (assets and resources) use the same interface
- **Context Passing**: All loading state encapsulated in single context object

### Loader Function Signatures

```cpp
// Asset loaders
template<Stream S> auto LoadGeometryAsset(LoaderContext<S> context) -> std::unique_ptr<GeometryAsset>;
template<Stream S> auto LoadMaterialAsset(LoaderContext<S> context) -> std::unique_ptr<MaterialAsset>;

// Resource loaders
template<Stream S> auto LoadBufferResource(LoaderContext<S> context) -> std::unique_ptr<BufferResource>;
template<Stream S> auto LoadTextureResource(LoaderContext<S> context) -> std::unique_ptr<TextureResource>;
```

### Dependency Registration

Dependencies are registered inline during loading, but safe unloading is not yet implemented:

```cpp
// Asset dependencies (material references) - REGISTRATION ONLY
if (material_key != AssetKey{} && context.asset_loader) {
  context.asset_loader->AddAssetDependency(context.current_asset_key, material_key);
}

// Resource dependencies (buffer/texture references) - REGISTRATION ONLY
if (vertex_buffer_index != 0 && context.asset_loader) {
  context.asset_loader->AddResourceDependency(context.current_asset_key, vertex_buffer_index);
}
```

**Note**: Dependencies are tracked during loading but there's no validation during unload operations yet. This means assets can still be unloaded even if other assets depend on them, potentially causing dangling references.

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
