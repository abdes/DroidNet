// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Assets.Resolvers;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks discovery and resolver metadata against the native catalog fixture.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class BuiltinCatalogProjectionTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>All native identities, including aliases, are discoverable without cooking.</summary>
    /// <returns>The asynchronous catalog projection test.</returns>
    [TestMethod]
    public async Task DiscoveryPreservesNativeIdentitiesAndAliasMetadata()
    {
        var native = await this.ReadCatalogAsync().ConfigureAwait(false);
        var records = native.CreateCatalogRecords();
        _ = records.Should().HaveCount(native.Geometries.Length + 1);
        foreach (var definition in native.Geometries)
        {
            var record = records.Single(item => item.Uri == definition.AssetUri);
            _ = record.Generated.Should().NotBeNull();
            _ = record.Generated!.CanonicalName.Should().Be(definition.CanonicalName);
            _ = record.Generated.CookedVirtualPath.Should().Be(definition.Contribution.VirtualPath);
            _ = record.Generated.DescriptorSchema.Should().Be(definition.Contribution.Descriptor.GetProperty("$schema").GetString());
        }

        var catalog = new GeneratedAssetCatalog(records);
        var aliases = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All, "IcoSphere"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = aliases.Select(static record => record.Name).Should().BeEquivalentTo(["IcoSphere", "GeodesicSphere"]);
    }

    /// <summary>Resolvers receive the native LOD/submesh definitions instead of a separate shape list.</summary>
    /// <returns>The asynchronous resolver projection test.</returns>
    [TestMethod]
    public async Task ResolverUsesEveryNativeLodAndSubmeshDeclaration()
    {
        var native = await this.ReadCatalogAsync().ConfigureAwait(false);
        var resolver = new GeneratedAssetResolver(native.CreateAssets());
        foreach (var definition in native.Geometries)
        {
            var resolved = await resolver.ResolveAsync(definition.AssetUri).ConfigureAwait(false);
            var geometry = resolved.Should().BeOfType<GeometryAsset>().Which;
            _ = geometry.Uri.Should().Be(definition.AssetUri);
            var lods = definition.Contribution.Descriptor.GetProperty("lods").EnumerateArray().ToArray();
            _ = geometry.Lods.Should().HaveCount(lods.Length);
            for (var index = 0; index < lods.Length; ++index)
            {
                _ = geometry.Lods[index].LodIndex.Should().Be(index);
                _ = geometry.Lods[index].SubMeshes.Select(static mesh => mesh.Name).Should()
                    .Equal(lods[index].GetProperty("submeshes").EnumerateArray().Select(static mesh => mesh.GetProperty("name").GetString()));
                _ = geometry.Lods[index].SubMeshes.Should().OnlyContain(static mesh => mesh.MaterialIndex == 0);
            }
        }
    }

    /// <summary>Changes in native LOD and submesh metadata flow through without managed defaults.</summary>
    /// <returns>The asynchronous metadata-shape regression.</returns>
    [TestMethod]
    public async Task ProjectionFollowsNativeLodAndSubmeshChanges()
    {
        var document = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var geometry = document["geometries"]!.AsArray()[0]!;
        var lods = geometry["descriptor"]!["lods"]!.AsArray();
        var second = lods[0]!.DeepClone();
        second["submeshes"]!.AsArray()[0]!["name"] = "Native second LOD";
        lods.Add(second);
        var catalog = BuiltinGeometryCatalog.Parse(document.ToJsonString());
        var projected = catalog.CreateAssets().OfType<GeometryAsset>().Single(asset => asset.Uri == catalog.Geometries[0].AssetUri);
        _ = projected.Lods.Should().HaveCount(2);
        _ = projected.Lods[1].SubMeshes.Should().ContainSingle().Which.Name.Should().Be("Native second LOD");
    }

    private async Task<BuiltinGeometryCatalog> ReadCatalogAsync()
        => BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
}
