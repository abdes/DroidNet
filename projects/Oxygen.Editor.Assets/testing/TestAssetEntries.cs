// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Testing;

/// <summary>The asset entries the feed publishes for common assets, for the tests of the parts that show assets.</summary>
public static class TestAssetEntries
{
    /// <summary>Gets a material of the Content mount whose cooked output is current.</summary>
    /// <param name="index">The material's number, which names it (<c>Material 00</c>).</param>
    /// <returns>The material's entry.</returns>
    public static AssetEntry CookedMaterial(int index)
    {
        var name = string.Create(CultureInfo.InvariantCulture, $"Material {index:00}");
        var identity = AssetPath.Create("Content", name + ".omat");
        var file = FileSystemPath.From("C:/Query" + identity.VirtualPath + AssetPath.AuthoringSuffix);
        return new(identity, name, AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Unknown, identity.VirtualPath, file, file, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true)
        {
            CookStatus = new(identity, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], []),
        };
    }

    /// <summary>Gets a model source, <c>/Content/Models/Crate/model.gltf</c>, before or after its import.</summary>
    /// <param name="imported">Whether the model is imported, with a geometry, a material and a scene as its outputs.</param>
    /// <returns>The model's entry.</returns>
    public static AssetEntry ImportedModel(bool imported)
    {
        var asset = AssetPath.Parse("/Content/Models/Crate/model.gltf");
        return new(asset, "model.gltf", AssetKind.Model, AssetState.Source, DerivedState: null, AssetRuntimeAvailability.NotApplicable, asset.VirtualPath, FileSystemPath.From("C:/Project/Content/Models/Crate/model.gltf"), DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            ImportSource = imported ? asset : null,
            CookStatus = imported
                ? new(
                    asset,
                    AssetCookFreshness.Current,
                    HasPublishedOutput: true,
                    OutputAvailability: CookedOutputAvailability.Present,
                    [
                        new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/Main.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Models/Crate/model.gltf/Main.ogeo"),
                        new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/Paint.omat"), ContentCookAssetKind.Material, "Content", "/Content/Models/Crate/model.gltf/Paint.omat"),
                        new(asset, AssetPath.Parse("/Content/Models/Crate/model.gltf/model.oscene"), ContentCookAssetKind.Scene, "Content", "/Content/Models/Crate/model.gltf/model.oscene"),
                    ],
                    [])
                : null,
        };
    }

    /// <summary>Gets the entries of an imported model's outputs, each naming the model as its import source.</summary>
    /// <param name="model">An imported model, from <see cref="ImportedModel"/>.</param>
    /// <returns>The outputs' entries.</returns>
    public static AssetEntry[] ImportedOutputs(AssetEntry model)
    {
        ArgumentNullException.ThrowIfNull(model);
        var status = model.CookStatus ?? throw new ArgumentException("The model is not imported.", nameof(model));
        return [.. status.Outputs.Select(output => new AssetEntry(
            output.CookedAssetUri,
            Path.GetFileNameWithoutExtension(output.VirtualPath),
            output.Kind switch
            {
                ContentCookAssetKind.Geometry => AssetKind.Geometry,
                ContentCookAssetKind.Material => AssetKind.Material,
                _ => AssetKind.Scene,
            },
            AssetState.Cooked,
            DerivedState: null,
            AssetRuntimeAvailability.NotMounted,
            output.VirtualPath,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            ImportSource = model.Identity,
            CookStatus = status with { AssetUri = output.CookedAssetUri },
        })];
    }
}
