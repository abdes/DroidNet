# Oxygen.Editor.Assets

Asset system foundation for the Oxygen editor, providing asset references, resolvers, and management services.

## Features

### Phase 4 (Current)
- **Asset Domain Models**: Base `Asset` class with `GeometryAsset` and `MaterialAsset` implementations
- **Asset References**: `AssetReference<T>` with automatic URI/Asset synchronization
- **Resolution Architecture**: Pluggable resolver pattern with `IAssetService` and `IAssetResolver`
- **Built-in Assets**: Generated primitives (Cube, Sphere, Plane, Cylinder) and default material via `GeneratedAssetResolver`
- **Future-Ready Stubs**: `FileSystemAssetResolver` and `PakAssetResolver` for upcoming phases

## URI Scheme

Assets are referenced using URIs in the format: `asset://<MountPoint>/<Path>`

### Mount Points
- **Generated**: Runtime procedural assets (e.g., `asset://Generated/BasicShapes/Cube`)
- **Content**: User project assets (e.g., `asset://Content/Models/Hero.geo`)
- **Engine**: Built-in engine resources from `oxygen.pak`
- **Packages**: External package content (e.g., `asset://Oxygen.StandardAssets/Skybox`)

## Usage

```csharp
// Create an asset reference
var geometryRef = new AssetReference<GeometryAsset>
{
    Uri = "asset://Generated/BasicShapes/Cube"
};

// Load the asset (consumer's responsibility)
geometryRef.Asset = await assetService.LoadAssetAsync<GeometryAsset>(geometryRef.Uri);
```

## Architecture

- **Decoupled Design**: `AssetReference<T>` has no dependency on `IAssetService`
- **Consistency**: Automatic synchronization between URI and Asset properties
- **Extensibility**: New resolvers can be registered at runtime
- **Thread-Safe**: `GeneratedAssetResolver` uses `FrozenDictionary` for lock-free reads

## Future Phases
- File system asset loading (Phase 5+)
- PAK file integration (Phase 5+)
- Asset metadata caching
- Asset hot-reloading
- Texture and shader asset types
