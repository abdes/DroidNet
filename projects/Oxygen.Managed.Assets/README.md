# Oxygen.Managed.Assets

Shared asset identities, authoring models and catalog queries for the editor.
Start with [catalogs](#catalogs), [authoring](#authoring) or
[cooked metadata](#cooked-metadata).

## Catalogs

`IAssetCatalog` provides scoped enumeration and change notifications. Providers
cover authoring files, engine-generated assets and native loose cooked indexes.
The Content Browser combines their records with project mount order and cooking
status.

```csharp
var scope = new AssetQueryScope(
    Roots: [new Uri("asset:///Content/Textures/")],
    Traversal: AssetQueryTraversal.Descendants);
var results = await catalog.QueryAsync(new AssetQuery(scope, SearchText: "wood"));
```

Project assets use `asset:///Content/...`; generated engine assets use
`asset:///Engine/Generated/...`. Preserve these identities when selecting,
referencing or displaying assets.

## Authoring

`Authoring/Materials` owns material source models and JSON serialization.
`Model` contains the editor-facing asset models. Native source inspection,
import, texture cooking and packaging are coordinated by
[Oxygen.Editor.ContentPipeline](../Oxygen.Editor.ContentPipeline/README.md)
through the shared native tools.

Use the [content pipeline LLD](../../design/editor/lld/content-pipeline.md) for
authoring and cooking workflows, and [virtual paths](docs/virtual-paths.md) for
asset naming. Binary runtime formats are owned by native Data and Content.

## Cooked metadata

[The v3 index reader](src/Persistence/LooseCooked/V3/README.md) supplies catalog
metadata without launching a tool. It reads the current native format only;
recook content when the format changes. Catalog metadata preserves each asset's
opaque native reference-table locator and counts without interpreting the block.

Native Cooker owns binary production. Native Content and Inspector own complete
cooked-root integrity verification. Reading an index for display does not verify
its payload files or make the content ready for publication.
