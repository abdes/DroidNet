// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Resolvers;

namespace Oxygen.Assets.Tests;

/// <summary>
/// Unit tests for the <see cref="GeneratedAssetResolver"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class GeneratedAssetResolverTests
{
    [TestMethod]
    [DataRow("Generated")]
    [DataRow("GENERATED")]
    [DataRow("generated")]
    public void CanResolve_WithGeneratedAuthority_ShouldReturnTrue(string authority)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = resolver.CanResolve(authority);

        // Assert
        result.Should().BeTrue();
    }

    [TestMethod]
    [DataRow("Content")]
    [DataRow("Engine")]
    [DataRow("Package")]
    public void CanResolve_WithOtherAuthority_ShouldReturnFalse(string authority)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = resolver.CanResolve(authority);

        // Assert
        result.Should().BeFalse();
    }

    [TestMethod]
    [DataRow("asset://Generated/BasicShapes/Cube")]
    [DataRow("asset://Generated/BasicShapes/Sphere")]
    [DataRow("asset://Generated/BasicShapes/Plane")]
    [DataRow("asset://Generated/BasicShapes/Cylinder")]
    [SuppressMessage("Design", "CA1054:URI-like parameters should not be strings", Justification = "test data")]
    public async Task ResolveAsync_WithBuiltInGeometry_ShouldReturnGeometryAsset(string uri)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        result.Should().NotBeNull();
        result.Should().BeOfType<GeometryAsset>();
        result!.Uri.Should().Be(uri);
        var geometry = (GeometryAsset)result;
        geometry.Lods.Should().HaveCount(1);
        geometry.Lods[0].LodIndex.Should().Be(0);
        geometry.Lods[0].SubMeshes.Should().HaveCount(1);
        geometry.Lods[0].SubMeshes[0].Name.Should().Be("Main");
    }

    [TestMethod]
    public async Task ResolveAsync_WithDefaultMaterial_ShouldReturnMaterialAsset()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset://Generated/Materials/Default";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        result.Should().NotBeNull();
        result.Should().BeOfType<MaterialAsset>();
        result!.Uri.Should().Be(uri);
    }

    [TestMethod]
    public async Task ResolveAsync_WithUnknownUri_ShouldReturnNull()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset://Generated/Unknown/Asset";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        result.Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveAsync_IsCaseSitive()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset://GENERATED/BASICSHAPES/CUBE";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        result.Should().BeNull();
    }
}
