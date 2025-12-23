# Oxygen.Assets

Non-UI asset pipeline primitives for Oxygen.

This project provides:

- Canonical asset identity via `asset://{MountPoint}/{Path}` URIs
- Asset loading via `IAssetService` + `IAssetResolver` (per mount point authority)
- Catalog/query contracts for enumeration via `IAssetCatalog` + `AssetQuery` (client-controlled scope)

## Key concepts

- **Asset URI**: `asset://Content/Textures/Wood01` or `asset://Generated/BasicShapes/Cube`
- **Mount point**: the URI authority (e.g. `Content`, `Engine`, `Generated`)

## Usage

Register one or more resolvers and load an asset by URI:

```csharp
IAssetService assetService = /* resolve from DI */;

assetService.RegisterResolver(new Oxygen.Assets.Resolvers.GeneratedAssetResolver());
// assetService.RegisterResolver(new Oxygen.Assets.Resolvers.FileSystemAssetResolver());

var cube = await assetService.LoadAssetAsync<Oxygen.Assets.GeometryAsset>(
 new Uri("asset://Generated/BasicShapes/Cube"));
```

Enumerate assets with a caller-specified scope:

```csharp
IAssetCatalog catalog = /* resolve from DI */;

var scope = new AssetQueryScope(
 Roots: [new Uri("asset://Content/Textures/")],
 Traversal: AssetQueryTraversal.Descendants);

var results = await catalog.QueryAsync(new AssetQuery(scope, SearchText: "wood"));
```

Notes:

- Loose filesystem is just one possible catalog provider; others can include generated assets and container/PAK indexes.
- Grouping (e.g., “frequently used”, “by mount point”) is typically a UI concern layered on top of query results.
