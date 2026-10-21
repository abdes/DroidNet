# Oxygen.Managed.Assets

Shared asset identities, authoring models and catalog queries for the editor.
Start with [catalogs](#catalogs), [authoring](#authoring) or
[cooked metadata](#cooked-metadata).

## Catalogs

`IAssetCatalog` provides scoped enumeration and change notifications. Providers
cover authoring files, loose cooked indexes and paks, including the engine's
`engine.pak`. The Content Explorer combines their records with project mount
order and cooking status.

```csharp
var scope = new AssetQueryScope(
    Roots: [AssetPath.Parse("/Content/Textures")],
    Traversal: AssetQueryTraversal.Descendants);
var results = await catalog.QueryAsync(new AssetQuery(scope, SearchText: "wood"));
```

Assets are identified by their virtual path (`AssetPath`), file type included:
`/Content/Materials/Wood.omat` for project content,
`/Engine/Generated/BasicShapes/Cube.ogeo` for engine content. Preserve these
identities when selecting, referencing or displaying assets.

## Authoring

`Authoring/Materials` and `Authoring/Textures` own the material and texture
descriptor models, `*.omat.json` and `*.otex.json`, with their readers and
writers. Both check a descriptor against the engine schema embedded in this
library and report problems by code (`MaterialSourceCodes`,
`TextureSourceCodes`); every editor module reads these formats through them.
Scenes link to assets by URI only; this library has no asset object model.
Native source inspection, import, texture cooking and packaging are coordinated
by [Oxygen.Editor.ContentPipeline](../Oxygen.Editor.ContentPipeline/README.md)
through the shared native tools.

Use the [content pipeline LLD](../../design/editor/lld/content-pipeline.md) for
authoring and cooking workflows, and the engine's
[virtual path rules](../Oxygen.Engine/src/Oxygen/Content/Docs/virtual_paths.md),
implemented for the editor by `Oxygen.Managed.Core`'s `VirtualPath`, for asset
naming. Binary runtime formats are owned by native Data and Content.

## Cooked metadata

[The loose index reader](src/Persistence/LooseCooked/README.md) and
[the pak reader](src/Persistence/Pak/README.md) supply catalog metadata without
launching a tool. They read the current native formats only; recook or repack
content when a format changes. `CookedAssetMetadata` names the asset's container
(a loose cooked root or a pak file); the descriptor file and its digest exist
only in a loose root. Catalog metadata preserves each asset's opaque native
reference-table locator and counts without interpreting the block.

Native Cooker owns binary production. Native Content and Inspector own complete
cooked-container integrity verification. Reading an index or a pak for display
does not verify its payload files or make the content ready for publication.
