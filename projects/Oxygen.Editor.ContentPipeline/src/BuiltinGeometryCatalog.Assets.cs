// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Projects native recipes into discovery and resolver metadata.</summary>
public sealed partial class BuiltinGeometryCatalog
{
    /// <summary>Creates discovery rows for every native geometry identity and the default material.</summary>
    /// <returns>Immutable records retaining aliases and source-to-cooked mapping.</returns>
    public IReadOnlyList<AssetRecord> CreateCatalogRecords()
    {
        var records = this.Geometries.Select(definition => new AssetRecord(definition.AssetUri)
        {
            Generated = CreateMetadata(definition.CanonicalName, definition.Contribution),
        }).ToList();
        records.Add(new AssetRecord(AssetUris.BuildGeneratedUri("Materials/Default"))
        {
            Generated = CreateMetadata("Default", this.DefaultMaterial),
        });
        return records.AsReadOnly();
    }

    /// <summary>Creates asset metadata from the engine's LOD, submesh and material declarations.</summary>
    /// <returns>Metadata for each native identity, without generating geometry or cooking content.</returns>
    public IReadOnlyList<Asset> CreateAssets()
    {
        var assets = this.Geometries.Select(definition => (Asset)new GeometryAsset
        {
            Uri = definition.AssetUri,
            Lods = definition.Contribution.Descriptor.GetProperty("lods").EnumerateArray().Select((lod, index) => new MeshLod
            {
                LodIndex = index,
                SubMeshes = lod.GetProperty("submeshes").EnumerateArray().Select(submesh => new SubMesh
                {
                    Name = submesh.GetProperty("name").GetString() ?? throw new InvalidDataException("A native submesh has no name."),
                    MaterialIndex = string.Equals(submesh.GetProperty("material_ref").GetString(), this.DefaultMaterial.VirtualPath, StringComparison.Ordinal)
                        ? 0 : throw new InvalidDataException("A native generated submesh references a material outside its catalog."),
                }).ToArray(),
            }).ToArray(),
        }).ToList();
        assets.Add(new MaterialAsset { Uri = AssetUris.BuildGeneratedUri("Materials/Default") });
        return assets.AsReadOnly();
    }

    private static GeneratedAssetMetadata CreateMetadata(string canonicalName, BuiltinDescriptorContribution contribution)
        => new(
            canonicalName,
            contribution.Descriptor.GetProperty("$schema").GetString() ?? throw new InvalidDataException("A native recipe has no descriptor schema."),
            contribution.VirtualPath);
}
