# Asset Dependency Validation and Caching Design

---

## Current Status

**✅ Implemented**: Unified cache for assets and resources with reference counting (`content_cache_`); forward and reverse dependency tracking; safe unloading and cascading release (`ReleaseAssetTree`); loader and unload functions for all supported asset/resource types
**❌ Not Implemented**: Hot-reload and memory budget tracking; priority-based eviction (LRU, etc.); circular dependency detection; asynchronous loading (all logic is synchronous)

## Core Philosophy

**Unified Approach**: Dependency tracking and asset caching are the same problem. Reference counting IS the dependency tracking mechanism. One class handles both concerns rather than artificial separation.

## Design Principles

1. **Reference Counting**: Track usage counts for shared assets (extends current dependency registration)
2. **Automatic Cascading**: Both loading and unloading cascade through dependency graphs automatically
3. **Safe Unloading**: Cannot unload an asset/resource if anything still references it
4. **In-Order Processing**: Dependencies loaded/unloaded in correct topological order

## Implementation Overview

**Extends Current AssetLoader**: Add caching and reference counting to existing LoaderContext system.

```cpp
class AssetLoader {
  // NEW: Main cache interface
  template<typename T> std::shared_ptr<T> Load(AssetKey key);
  void Release(AssetKey key);

  // EXISTING: Dependency registration (already implemented via LoaderContext)
  void AddAssetDependency(AssetKey dependent, AssetKey dependency);      // Asset→Asset
  void AddResourceDependency(AssetKey dependent, ResourceIndexT dependency); // Asset→Resource

  // NEW: Unloader function registration - specialized cleanup per type
  template<typename T> void RegisterUnloader(UnLoadFunction<T> unloader_fn);

private:
  // NEW: Asset cache implementation
  struct AssetEntry {
    std::shared_ptr<void> asset;
    int ref_count = 0;
  };

  // NEW: Cache state
  mutable std::mutex cache_mutex_;
  std::unordered_map<AssetKey, AssetEntry> asset_cache_;

  // NEW: Dependency tracking (extends current registration)
  std::unordered_map<AssetKey, std::unordered_set<AssetKey>> dependents_of_asset_;
  std::unordered_map<ResourceIndexT, std::unordered_set<AssetKey>> dependents_of_resource_;

  // EXISTING: Current AssetLoader state
  std::vector<std::unique_ptr<PakFile>> paks_;
  std::unordered_map<TypeId, LoaderFnErased> loaders_;
  // (existing LoaderContext infrastructure already works)
};

// NEW: Unloader function signatures
template<typename T>
using UnLoadFunction = std::function<void(std::shared_ptr<T>, AssetLoader&)>;
```

### Dependency Types

**Asset → Asset Dependencies** (`AssetKey → AssetKey`):

- GeometryAsset depends on MaterialAsset
- **Current**: Registered via `AddAssetDependency()` in LoaderContext ✅
- **Missing**: Reference counting and safe unload validation ❌

**Asset → Resource Dependencies** (`AssetKey → ResourceIndexT`):

- MaterialAsset depends on TextureResource, GeometryAsset depends on BufferResource
- **Current**: Registered via `AddResourceDependency()` in LoaderContext ✅
- **Missing**: Resource reference tracking and coordination with ResourceTable ❌

**Note**: Resources never depend on Assets (resources are lower-level primitives)

## Key Scenarios

### Loading with Automatic Caching

```cpp
// User request
auto geometry = loader.Load<GeometryAsset>(geometry_key);

// Proposed implementation (builds on current LoaderContext):
// 1. Check cache: if cached, increment ref_count and return
// 2. If not cached: Use existing LoaderContext to load asset
// 3. Dependencies already registered during loading (current implementation)
// 4. Store in cache with ref_count = 1
// 5. Return shared_ptr to asset
```

### Safe Unloading with Dependency Validation

```cpp
// User releases asset
loader.Release(geometry_key);

// Proposed cascade cleanup:
// 1. geometry.ref_count-- (may become 0)
// 2. NEW: Check HasDependents(geometry_key) - validate safe to unload
// 3. NEW: Call specialized unloader for cleanup
// 4. NEW: Cascade release dependencies automatically
// 5. Continue until no more cleanup possible
```

### Shared Asset Protection (NEW Capability)

```cpp
// Multiple assets reference same material
auto geo1 = loader.Load<GeometryAsset>(geo1_key);  // material.ref_count = 1
auto geo2 = loader.Load<GeometryAsset>(geo2_key);  // material.ref_count = 2

loader.Release(geo1_key);  // material.ref_count = 1 (protected!)
loader.Release(geo2_key);  // material.ref_count = 0 (now safe to cleanup)
```

## Critical Implementation Details

### Core Cache Operations (TO BE IMPLEMENTED)

```cpp
// Store asset with reference counting
template<typename T>
void StoreAsset(const AssetKey& key, std::shared_ptr<T> asset) {
  std::lock_guard lock(cache_mutex_);
  auto it = asset_cache_.find(key);
  if (it != asset_cache_.end()) {
    it->second.ref_count++;  // Already cached - increment
    return;
  }
  asset_cache_[key] = AssetEntry{ .asset = std::static_pointer_cast<void>(asset), .ref_count = 1 };
}

// Release with cascading cleanup
void Release(const AssetKey& key) {
  std::lock_guard lock(cache_mutex_);
  auto it = asset_cache_.find(key);
  if (it == asset_cache_.end()) return;

  it->second.ref_count--;
  if (it->second.ref_count == 0 && !HasDependents(key)) {
    CallUnloaderFor(key, it->second.asset);  // NEW: Specialized cleanup
    asset_cache_.erase(it);
    CascadeReleaseDependencies(key);         // NEW: Automatic cascade
  }
}
```

### Dependency Validation (TO BE IMPLEMENTED)

```cpp
// Check if any assets depend on the given asset key
bool HasDependents(const AssetKey& key) const {
  auto it = dependents_of_asset_.find(key);
  return it != dependents_of_asset_.end() && !it->second.empty();
}

// Check if any assets depend on the given resource
bool HasResourceDependents(ResourceIndexT resource_id) const {
  auto it = dependents_of_resource_.find(resource_id);
  return it != dependents_of_resource_.end() && !it->second.empty();
}
```

### Integration with Current LoaderContext

**No Changes Needed**: Current LoaderContext and dependency registration works as-is.

**Extension Point**: AssetLoader methods that currently call loaders directly will be extended to check cache first, then call existing loader functions if needed.

## Design Advantages

1. **Builds on Existing Work**: Extends current LoaderContext system without breaking changes
2. **Safety**: Impossible to unload something still in use (new capability)
3. **Automatic**: Cascading load/unload reduces manual management (new capability)
4. **Performance**: Shared assets cached, duplicate loading eliminated (new capability)
5. **Debugging**: Clear dependency graphs for asset analysis tools

## Design Limitations

1. **Manual Registration**: Loaders must explicitly register dependencies (already implemented, works well)
2. **No Circular Detection**: Circular dependencies not detected (should be avoided in asset design)
3. **No Priority**: No priority-based eviction (LRU, size-based, etc.)
4. **No Async**: Current design is synchronous (async version needs additional design)

## Implementation Priority

### Phase 1: Basic Asset Caching

1. Add `asset_cache_` to AssetLoader
2. Implement `Load<T>()` and `Release()` methods
3. Add reference counting (without dependency validation)

### Phase 2: Safe Unloading

1. Implement `HasDependents()` validation
2. Add dependency reversal maps (dependents_of_asset_, dependents_of_resource_)
3. Implement cascading release

### Phase 3: Specialized Unloaders

1. Add unloader function registration
2. Implement GPU resource cleanup integration
3. Add memory budget tracking
