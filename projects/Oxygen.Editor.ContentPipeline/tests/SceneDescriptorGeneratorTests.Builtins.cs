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
    /// <summary>Preserves every native descriptor field, including thin bounds and default material parameters.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task AllBuiltinContributionsPreserveNativePayloadsAndIdentity()
    {
        using var workspace = new TempWorkspace();
        var provider = new BuiltinCatalogFixture();
        var catalog = await provider.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = new ProceduralGeometryDescriptorService(provider);

        var inputs = await service.EnsureDescriptorsAsync(CreateScope(workspace), catalog.Geometries.Select(static item => item.AssetUri).ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Should().HaveCount(12);
        foreach (var definition in catalog.Geometries)
        {
            var input = inputs.Single(input => input.AssetUri == definition.AssetUri);
            _ = input.OutputVirtualPath.Should().Be(definition.Contribution.VirtualPath);
            var written = JsonNode.Parse(await File.ReadAllTextAsync(input.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
            _ = JsonNode.DeepEquals(written, JsonNode.Parse(definition.Contribution.Descriptor.GetRawText())).Should().BeTrue(definition.Name);
        }

        var material = inputs.Single(static input => input.Kind == ContentCookAssetKind.Material);
        var materialJson = JsonNode.Parse(await File.ReadAllTextAsync(material.SourceAbsolutePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = JsonNode.DeepEquals(materialJson, JsonNode.Parse(catalog.DefaultMaterial.Descriptor.GetRawText())).Should().BeTrue();
        _ = catalog.Find(AssetUris.BuildGeneratedUri("BasicShapes/GeodesicSphere"))!.CanonicalName.Should().Be("IcoSphere");
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
