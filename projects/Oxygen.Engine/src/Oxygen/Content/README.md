# Oxygen Content System - PAK File Tooling

## Asset Management Feature Status

### Core Infrastructure

| Feature | Status | Implementation | Notes |
|---------|--------|----------------|-------|
| **PAK File Format** | ✅ **Complete** | `PakFile.h/cpp` | Binary container with asset directory, resource tables |
| **Asset Directory** | ✅ **Complete** | `PakFormat.h` | Asset key → metadata mapping |
| **Resource Tables** | ✅ **Complete** | `ResourceTable.h` | Type-safe buffer/texture resource access |
| **Asset Key System** | ✅ **Complete** | `AssetKey.h` | 16-byte GUID-based asset identification |
| **Asset Type System** | ✅ **Complete** | `AssetType.h` | Extensible asset type enumeration |

### Asset Loading Pipeline

| Feature | Status | Implementation | Notes |
|---------|--------|----------------|-------|
| **Synchronous Asset Loader** | ✅ **Complete** | `AssetLoader.h/cpp` | Type-safe LoaderContext system |
| **Loader Registration** | ✅ **Complete** | `LoaderFunctions.h` | Unified LoaderContext API for all loaders |
| **Resource Caching** | ✅ **Complete** | `ResourceTable.h` | Resource deduplication with manual eviction |
| **Dependency Registration** | ✅ **Complete** | `LoaderContext` | Inline dependency registration during loading |
| **Safe Asset Unloading** | ❌ **Missing** | *Not implemented* | No dependency checking for unload operations |
| **Asset Caching** | ❌ **Missing** | *Not implemented* | Assets not cached in AssetLoader |
| **Hot Reload** | ❌ **Missing** | *Not implemented* | No file watching or invalidation |

### Asynchronous System (Designed but Not Implemented)

| Feature | Status | Implementation | Notes |
|---------|--------|----------------|-------|
| **Coroutine-based API** | ❌ **Missing** | *Documented only* | C++20 coroutines + Corral library |
| **ThreadPool Integration** | 🔄 **Partial** | `OxCo/ThreadPool.h` | ThreadPool exists but not integrated |
| **Async File I/O** | ❌ **Missing** | *Not implemented* | No async disk operations |
| **GPU Upload Queue** | ❌ **Missing** | *Not implemented* | No dedicated upload pipeline |
| **Background Processing** | ❌ **Missing** | *Not implemented* | No CPU-bound work offloading |

### Asset Types (Loaders)

| Asset Type | Status | Implementation | Features |
|------------|--------|----------------|----------|
| **GeometryAsset** | ✅ **Complete** | `GeometryLoader.h` | Multi-LOD meshes, submeshes, LoaderContext |
| **BufferResource** | ✅ **Complete** | `BufferLoader.h` | Vertex/index/constant buffers, LoaderContext |
| **TextureResource** | ✅ **Complete** | `TextureLoader.h` | 2D/3D/cubemap textures, LoaderContext |
| **MaterialAsset** | ✅ **Complete** | `MaterialLoader.h` | Shader + texture refs, LoaderContext |
| **SceneAsset** | ❌ **Missing** | *Not implemented* | Scene composition and hierarchy |
| **AnimationAsset** | ❌ **Missing** | *Not implemented* | Animation sequences |
| **AudioResource** | ❌ **Missing** | *Not implemented* | Compressed audio data |

### Streaming & Chunking

| Feature | Status | Implementation | Notes |
|---------|--------|----------------|-------|
| **Chunked Loading** | ❌ **Missing** | *Documented only* | Large asset streaming |
| **Memory Mapping** | ❌ **Missing** | *Not implemented* | Direct file-to-memory mapping |
| **GPU Alignment** | ✅ **Complete** | PAK format | 256-byte alignment for GPU resources |
| **Progressive Loading** | ❌ **Missing** | *Not implemented* | Priority-based asset streaming |
| **Residency Management** | ❌ **Missing** | *Not implemented* | GPU memory budget tracking |

### Development Tools

| Tool | Status | Implementation | Purpose |
|------|--------|----------------|---------|
| **PAK Generator** | ✅ **Complete** | `generate_pak.py` | YAML → binary PAK conversion |
| **PAK Dumper** | ✅ **Complete** | `PakFileDumper.cpp` | PAK inspection and debugging |
| **Performance Profiler** | ❌ **Missing** | *Not implemented* | Loading time and memory analysis |
| **Dependency Analyzer** | ❌ **Missing** | *Not implemented* | Asset reference graph analysis |

### Testing & Validation

| Area | Status | Coverage | Notes |
|------|--------|----------|-------|
| **Unit Tests** | ✅ **Excellent** | Comprehensive coverage | All loaders: basic, error, dependency tests |
| **Integration Tests** | ✅ **Good** | Link + table tests | LoaderContext integration validated |
| **Performance Tests** | ❌ **Missing** | *None* | No loading benchmarks |
| **Memory Tests** | ❌ **Missing** | *None* | No leak or usage validation |

## Priority Implementation Roadmap

### Phase 1: Foundation (High Priority)

1. **Asset Caching System** - Implement caching in AssetLoader (resources already cached)
2. **Safe Asset Unloading** - Dependency checking to prevent unloading assets with active references
3. **Reference Counting** - Track usage counts for shared assets (MaterialAssets, etc.)
4. **Error Handling** - Robust error recovery and reporting

### Phase 2: Async Pipeline (Medium Priority)

1. **Coroutine Integration** - Implement async loading API
2. **ThreadPool Integration** - Connect existing ThreadPool to asset loading
3. **GPU Upload Queue** - Dedicated GPU resource upload pipeline

### Phase 3: Advanced Features (Lower Priority)

1. **Hot Reload System** - File watching and asset invalidation
2. **Streaming System** - Chunked loading for large assets
3. **Memory Management** - Residency tracking and smart eviction

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

Since MaterialAssets can be shared across multiple GeometryAssets, the system
needs reference counting to safely unload materials without breaking geometry
that depends on them.
