# Oxygen.Assets

A compact asset metadata & resolution surface used by the Oxygen editor. This module provides
lightweight asset models, serializable references and a pluggable resolver architecture so
editor code can locate and load assets consistently.

![Phase 4](https://img.shields.io/badge/phase-4-yellow.svg)

---

## Quick overview

This package contains a small, well-scoped asset system used by the editor for metadata and
URI-based resolution. It intentionally focuses on editor-hosted metadata (not runtime-heavy
asset data) and ships Phase 4 building blocks: core types, a generated in-memory resolver, and
stubs for filesystem & PAK resolvers to be implemented in later phases.

---

### Table of contents

- [Quick overview](#quick-overview)
- [Implementation status](#implementation-status)
- [Asset URI scheme](#asset-uri-scheme)
- [Mount points](#mount-points)
- [Built-in generated assets](#built-in-generated-assets)
- [Key types and behavior (quick reference)](#key-types-and-behavior-quick-reference)
- [Usage examples](#usage-examples)

## Implementation status

| Area | Status | Notes |
|---|---:|---|
| Asset domain models | ✅ Implemented | `Asset`, `GeometryAsset`, `MaterialAsset`, `MeshLod`, `SubMesh` |
| Asset references | ✅ Implemented | `AssetReference<T>` keeps `Uri` and runtime `Asset` in sync (`[JsonIgnore]` for Asset) |
| Generated assets | ✅ Implemented | `GeneratedAssetResolver` exposes a frozen, thread-safe catalog (basic shapes + default material) |
| File-system resolver | ✅ Implemented | `FileSystemAssetResolver` maps URIs to source/imported files on disk |
| PAK/Engine resolver | ⚠️ Stub | `PakAssetResolver` is present as a placeholder (future PAK integration) |

> Note: resolver matching is case-insensitive (e.g. `Generated` and `generated` are equivalent).

## Asset URI scheme

Assets are referenced using canonical URIs:

```text
asset:///{MountPoint}/{Path}
```

Examples:

- `asset:///Generated/BasicShapes/Cube` — runtime-generated built-in geometry
- `asset:///Generated/Materials/Default` — built-in default material
- `asset:///Content/Models/Hero.ogeo` — content (file-system)
- `asset:///Engine/Textures/Skybox.otex` — engine/PAK resources

## Mount points

| Mount point | Implemented | Typical examples | Implementation |
|---|:---:|---|---|
| Generated | ✅ | `asset:///Generated/BasicShapes/Cube` | `GeneratedAssetResolver` — frozen in-memory catalog (Cube/Sphere/Plane/Cylinder + Default material) |
| Content | ✅ | `asset:///Content/...` | `FileSystemAssetResolver` — configured for "Content" mount point, reads from source/imported folders |
| Engine | ✅ | `asset:///Engine/...` | `FileSystemAssetResolver` — configured for "Engine" mount point (in Editor) or `PakAssetResolver` (in Runtime) |

## Built-in generated assets

The resolver for the `Generated` mount point exposes a small, always-available set of assets:

| Asset path | Type | Notes |
|---|---:|---|
| `asset:///Generated/BasicShapes/Cube` | Geometry | Has 1 LOD with a single `SubMesh` named `Main` |
| `asset:///Generated/BasicShapes/Sphere` | Geometry | Has 1 LOD with a single `SubMesh` named `Main` |
| `asset:///Generated/BasicShapes/Plane` | Geometry | Has 1 LOD with a single `SubMesh` named `Main` |
| `asset:///Generated/BasicShapes/Cylinder` | Geometry | Has 1 LOD with a single `SubMesh` named `Main` |
| `asset:///Generated/Materials/Default` | Material | Minimal material metadata used by generated geometry |

## Key types and behavior (quick reference)

| Type | Purpose | Key details |
|---|---|---|
| `Asset` (abstract) | Base metadata type | `required Uri` property; `Name` derived from URI path's last segment |
| `GeometryAsset` | Geometry metadata | `IList<MeshLod> Lods` where each `MeshLod` has `LodIndex` and `IList<SubMesh>` |
| `SubMesh` | Mesh partition | `Name`, `MaterialIndex` (zero-based material index) |
| `MaterialAsset` | Minimal material metadata | Placeholder for future shader/texture fields |
| `AssetReference<T>` | Serializable reference | `Uri` is serialized; `Asset` is runtime-only `[JsonIgnore]`. Setting `Uri` invalidates `Asset` if different; setting `Asset` syncs `Uri`. |
| `IAssetResolver` | Resolver contract | `CanResolve(string mountPoint)` (case-insensitive) + `ResolveAsync(Uri)` → `Asset?` |
| `IAssetService` | Orchestrator | Registers resolvers and exposes `LoadAssetAsync<T>(Uri)` for typed loading |

## Usage examples

Basic: populate an `AssetReference<T>` with a URI and ask the `IAssetService` to load it at runtime.

```csharp
// serializable ref (Uri persisted) -> runtime Asset loaded by service
var geometryRef = new AssetReference<GeometryAsset> { Uri = new Uri("asset:///Generated/BasicShapes/Cube") };
geometryRef.Asset = await assetService.LoadAssetAsync<GeometryAsset>(geometryRef.Uri!);
```

Example: set the runtime asset directly — `AssetReference` will synchronize the `Uri` automatically.

```csharp
var builtIn = new GeometryAsset { Uri = new Uri("asset:///Generated/BasicShapes/Sphere"), Lods = new List<MeshLod>() };
var refToSphere = new AssetReference<GeometryAsset>();
refToSphere.Asset = builtIn; // refToSphere.Uri is updated to builtIn.Uri
```
