// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies descriptor forwarding for the complete engine-owned catalog.</summary>
public sealed partial class SceneDescriptorGeneratorTests
{
    /// <summary>An explicit builtin material uses its native identity even without builtin geometry.</summary>
    /// <param name="materialName">The case variant of the authored builtin reference.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Default")]
    [DataRow("default")]
    public async Task AuthoredGeometryWithDefaultOverrideUsesCatalogMaterialPath(string materialName)
    {
        using var workspace = new TempWorkspace();
        var scene = CreateScene(workspace.Project);
        var node = new Oxygen.Editor.World.SceneNode(scene) { Name = "Authored mesh" };
        var geometryUri = new Uri("asset:///Content/Geometry/Imported.ogeo");
        var geometry = new Oxygen.Editor.World.GeometryComponent
        {
            Name = "Geometry",
            Geometry = new Oxygen.Managed.Assets.Model.AssetReference<Oxygen.Managed.Assets.Model.GeometryAsset>(geometryUri),
        };
        geometry.OverrideSlots.Add(new Oxygen.Editor.World.Slots.MaterialsSlot
        {
            Target = new(geometryUri, Guid.NewGuid(), new string('a', 64)),
            Material = new Oxygen.Managed.Assets.Model.AssetReference<Oxygen.Managed.Assets.Model.MaterialAsset>(AssetUris.BuildGeneratedUri("Materials/" + materialName)),
        });
        _ = node.AddComponent(geometry);
        scene.RootNodes.Add(node);
        var provider = new BuiltinCatalogFixture();
        var catalog = await provider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var generator = new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(provider));
        var result = await generator.GenerateAsync(scene, CreateScope(workspace), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().BeEmpty();
        var descriptor = JsonNode.Parse(await File.ReadAllTextAsync(result.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        _ = descriptor["renderables"]![0]!["material_overrides"]![0]!["material_ref"]!.GetValue<string>().Should().Be(catalog.DefaultMaterial.VirtualPath);
        _ = result.Dependencies.Should().ContainSingle(input => input.AssetUri == AssetUris.BuildGeneratedUri("Materials/Default"));
    }

    /// <summary>Preserves every native descriptor field, including thin bounds and default material parameters.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task AllBuiltinContributionsPreserveNativePayloadsAndIdentity()
    {
        using var workspace = new TempWorkspace();
        var provider = new BuiltinCatalogFixture();
        var catalog = await provider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = new ProceduralGeometryDescriptorService(provider);

        var inputs = await service.EnsureDescriptorsAsync(CreateScope(workspace), catalog.AuthoringGeometries.Select(static item => item.AssetUri).ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Should().HaveCount(11);
        foreach (var definition in catalog.AuthoringGeometries)
        {
            var input = inputs.Single(input => input.AssetUri == definition.AssetUri);
            _ = input.OutputVirtualPath.Should().Be(definition.Contribution.VirtualPath);
            var written = JsonNode.Parse(await File.ReadAllTextAsync(input.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
            _ = JsonNode.DeepEquals(written, JsonNode.Parse(definition.Contribution.Descriptor.GetRawText())).Should().BeTrue(definition.Name);
        }

        var material = inputs.Single(static input => input.Kind == ContentCookAssetKind.Material);
        var materialJson = JsonNode.Parse(await File.ReadAllTextAsync(material.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = JsonNode.DeepEquals(materialJson, JsonNode.Parse(catalog.DefaultMaterial.Descriptor.GetRawText())).Should().BeTrue();
        _ = catalog.Find(AssetUris.BuildGeneratedUri("BasicShapes/IcoSphere"))!.CanonicalName.Should().Be("IcoSphere");
        _ = catalog.Find(AssetUris.BuildGeneratedUri("BasicShapes/ArrowGizmo")).Should().BeNull();
    }

    /// <summary>Does not fabricate a descriptor or default material for an unknown engine name.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task UnknownBuiltinHasNoFabricatedCookContribution()
    {
        using var workspace = new TempWorkspace();
        var service = new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture());

        var inputs = await service.EnsureDescriptorsAsync(CreateScope(workspace), [AssetUris.BuildGeneratedUri("BasicShapes/Unknown")], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Should().BeEmpty();
        _ = Directory.Exists(Path.Combine(workspace.Root, ".pipeline")).Should().BeFalse();
    }
}
