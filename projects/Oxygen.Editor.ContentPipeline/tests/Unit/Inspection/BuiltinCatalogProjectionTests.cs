// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class BuiltinCatalogProjectionTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Native authoring identities and categories are discoverable without exposing tool resources.</summary>
    /// <returns>The asynchronous catalog projection test.</returns>
    [TestMethod]
    public async Task DiscoveryPreservesCanonicalIdentitiesAndAuthoringCategories()
    {
        var native = await this.ReadCatalogAsync().ConfigureAwait(false);
        var records = native.CreateCatalogRecords();
        _ = records.Should().HaveCount(native.AuthoringGeometries.Count() + 1);
        foreach (var definition in native.AuthoringGeometries)
        {
            var record = records.Single(item => item.Uri == definition.AssetUri);
            _ = record.Generated.Should().NotBeNull();
            _ = record.Generated!.CanonicalName.Should().Be(definition.CanonicalName);
            _ = record.Generated.AuthoringCategory.Should().Be(definition.AuthoringCategory);
            _ = record.Generated.CookedVirtualPath.Should().Be(definition.Contribution.VirtualPath);
            _ = record.Generated.DescriptorSchema.Should().Be(definition.Contribution.Descriptor.GetProperty("$schema").GetString());
        }

        var catalog = new GeneratedAssetCatalog(records);
        var matches = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All, "IcoSphere"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = matches.Select(static record => record.Name).Should().BeEquivalentTo(["IcoSphere"]);
        _ = native.Geometries.Should().ContainSingle(definition => definition.AuthoringCategory == GeneratedAssetCategory.Internal);
        _ = records.Should().NotContain(record => record.Generated!.AuthoringCategory == GeneratedAssetCategory.Internal);
    }

    /// <summary>The native catalog's submeshes are named and use the catalog's default material.</summary>
    /// <returns>The asynchronous submesh declaration test.</returns>
    [TestMethod]
    public async Task ValidateSubmeshes_NativeCatalog_Passes()
    {
        var native = await this.ReadCatalogAsync().ConfigureAwait(false);

        var act = native.ValidateSubmeshes;

        _ = act.Should().NotThrow();
    }

    /// <summary>A submesh whose material is outside the catalog is rejected.</summary>
    /// <returns>The asynchronous submesh declaration regression.</returns>
    [TestMethod]
    public async Task ValidateSubmeshes_MaterialOutsideTheCatalog_Throws()
    {
        var document = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var geometry = document["geometries"]!.AsArray()[0]!;
        geometry["descriptor"]!["lods"]!.AsArray()[0]!["submeshes"]!.AsArray()[0]!["material_ref"] = "/Content/Materials/Elsewhere.omat";
        var catalog = BuiltinGeometryCatalog.Parse(document.ToJsonString());

        var act = catalog.ValidateSubmeshes;

        _ = act.Should().Throw<InvalidDataException>();
    }

    /// <summary>The native category is required and unknown values never become authoring choices.</summary>
    /// <param name="category">An invalid category, or null to remove the field.</param>
    /// <returns>The asynchronous parser regression.</returns>
    [TestMethod]
    [DataRow(null)]
    [DataRow("unsupported")]
    [DataRow("Standard")]
    public async Task CatalogRejectsMissingOrUnknownAuthoringCategory(string? category)
    {
        var document = JsonNode.Parse((await this.ReadCatalogAsync().ConfigureAwait(false)).ToJson())!;
        var geometry = document["geometries"]!.AsArray()[0]!.AsObject();
        if (category is null)
        {
            _ = geometry.Remove("authoring_category");
        }
        else
        {
            geometry["authoring_category"] = category;
        }

        var parse = () => BuiltinGeometryCatalog.Parse(document.ToJsonString());
        _ = parse.Should().Throw<InvalidDataException>();
    }

    /// <summary>Catalogs without the current classification contract are not accepted.</summary>
    /// <returns>The asynchronous version rejection check.</returns>
    [TestMethod]
    public async Task CatalogRejectsAnObsoleteVersion()
    {
        var document = JsonNode.Parse((await this.ReadCatalogAsync().ConfigureAwait(false)).ToJson())!;
        document["schema"] = "oxygen.builtin-geometry-catalog.v1";
        var parse = () => BuiltinGeometryCatalog.Parse(document.ToJsonString());
        _ = parse.Should().Throw<InvalidDataException>();
    }

    private async Task<BuiltinGeometryCatalog> ReadCatalogAsync()
        => BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
}
