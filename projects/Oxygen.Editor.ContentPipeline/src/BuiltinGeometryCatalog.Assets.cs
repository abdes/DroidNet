// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Projects native recipes into discovery and resolver metadata.</summary>
public sealed partial class BuiltinGeometryCatalog
{
    /// <summary>Creates discovery rows for native authoring choices and the default material.</summary>
    /// <returns>Immutable records retaining classification and source-to-cooked mapping.</returns>
    public IReadOnlyList<AssetRecord> CreateCatalogRecords()
    {
        var records = this.AuthoringGeometries.Select(definition => new AssetRecord(definition.AssetUri)
        {
            Generated = CreateMetadata(definition.CanonicalName, definition.Contribution, definition.AuthoringCategory),
        }).ToList();
        records.Add(new AssetRecord(AssetUris.BuildGeneratedUri("Materials/Default"))
        {
            Generated = CreateMetadata("Default", this.DefaultMaterial, GeneratedAssetCategory.Standard),
        });
        return records.AsReadOnly();
    }

    /// <summary>Checks the engine's LOD and submesh declarations of every authoring geometry.</summary>
    /// <exception cref="InvalidDataException">A submesh has no name, or uses a material outside the catalog.</exception>
    public void ValidateSubmeshes()
    {
        var submeshes = this.AuthoringGeometries
            .SelectMany(static definition => definition.Contribution.Descriptor.GetProperty("lods").EnumerateArray())
            .SelectMany(static lod => lod.GetProperty("submeshes").EnumerateArray());
        foreach (var submesh in submeshes)
        {
            if (submesh.GetProperty("name").GetString() is null)
            {
                throw new InvalidDataException("A native submesh has no name.");
            }

            if (!string.Equals(submesh.GetProperty("material_ref").GetString(), this.DefaultMaterial.VirtualPath, StringComparison.Ordinal))
            {
                throw new InvalidDataException("A native generated submesh references a material outside its catalog.");
            }
        }
    }

    private static GeneratedAssetMetadata CreateMetadata(string canonicalName, BuiltinDescriptorContribution contribution, GeneratedAssetCategory authoringCategory)
        => new(
            canonicalName,
            contribution.Descriptor.GetProperty("$schema").GetString() ?? throw new InvalidDataException("A native recipe has no descriptor schema."),
            contribution.VirtualPath,
            authoringCategory);
}
