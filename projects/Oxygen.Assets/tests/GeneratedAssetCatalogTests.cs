// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Catalog;

namespace Oxygen.Assets.Tests;

/// <summary>
/// Unit tests for the <see cref="GeneratedAssetCatalog"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class GeneratedAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_WithAllScope_ShouldReturnAllBuiltIns()
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();
        var query = new AssetQuery(AssetQueryScope.All);

        // Act
        var results = await catalog.QueryAsync(query, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().HaveCount(5);
        _ = results.Select(r => r.Uri)
            .Should().Contain([
                new Uri("asset:///Engine/Generated/BasicShapes/Cube"),
                new Uri("asset:///Engine/Generated/BasicShapes/Sphere"),
                new Uri("asset:///Engine/Generated/BasicShapes/Plane"),
                new Uri("asset:///Engine/Generated/BasicShapes/Cylinder"),
                new Uri("asset:///Engine/Generated/Materials/Default"),
            ]);
    }

    [TestMethod]
    public async Task QueryAsync_WithMountRootDescendants_ShouldReturnAllGeneratedAssets()
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();
        var scope = new AssetQueryScope(
            Roots: [new Uri("asset:///Engine/Generated/")],
            Traversal: AssetQueryTraversal.Descendants);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().HaveCount(5);
    }

    [TestMethod]
    public async Task QueryAsync_WithFolderRootDescendants_ShouldFilterToFolder()
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();
        var scope = new AssetQueryScope(
            Roots: [new Uri("asset:///Engine/Generated/BasicShapes/")],
            Traversal: AssetQueryTraversal.Descendants);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().HaveCount(4);
        _ = results.Select(r => r.Uri.AbsolutePath).Should().AllSatisfy(p => p.Should().StartWith("/Engine/Generated/BasicShapes/"));
    }

    [TestMethod]
    public async Task QueryAsync_WithSelfTraversal_ShouldMatchExactUriOnly()
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();
        var scope = new AssetQueryScope(
            Roots: [new Uri("asset:///Engine/Generated/Materials/Default")],
            Traversal: AssetQueryTraversal.Self);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().ContainSingle();
        _ = results[0].Uri.Should().Be(new Uri("asset:///Engine/Generated/Materials/Default"));
    }

    [TestMethod]
    public async Task QueryAsync_WithChildrenTraversal_ShouldMatchImmediateChildrenOnly()
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();
        var scope = new AssetQueryScope(
            Roots: [new Uri("asset:///Engine/Generated/BasicShapes/")],
            Traversal: AssetQueryTraversal.Children);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().HaveCount(4);
        _ = results.Select(r => r.Uri).Should().Contain([
            new Uri("asset:///Engine/Generated/BasicShapes/Cube"),
            new Uri("asset:///Engine/Generated/BasicShapes/Sphere"),
            new Uri("asset:///Engine/Generated/BasicShapes/Plane"),
            new Uri("asset:///Engine/Generated/BasicShapes/Cylinder"),
        ]);
    }

    [TestMethod]
    [DataRow("Cube")]
    [DataRow("cube")]
    public async Task QueryAsync_WithSearchText_ShouldFilterResults(string term)
    {
        // Arrange
        var catalog = new GeneratedAssetCatalog();

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All, SearchText: term), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Should().ContainSingle();
        _ = results[0].Uri.Should().Be(new Uri("asset:///Engine/Generated/BasicShapes/Cube"));
    }
}
