// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Model;
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
    [DataRow("Engine")]
    [DataRow("ENGINE")]
    public void CanResolve_WithEngineMountPoint_ShouldReturnTrue(string mountPoint)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = resolver.CanResolve(mountPoint);

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    [DataRow("Content")]
    [DataRow("Package")]
    [DataRow("Generated")]
    public void CanResolve_WithOtherMountPoint_ShouldReturnFalse(string mountPoint)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = resolver.CanResolve(mountPoint);

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    [DataRow("asset:///Engine/Generated/BasicShapes/Cube")]
    [DataRow("asset:///Engine/Generated/BasicShapes/Sphere")]
    [DataRow("asset:///Engine/Generated/BasicShapes/Plane")]
    [DataRow("asset:///Engine/Generated/BasicShapes/Cylinder")]
    [SuppressMessage("Design", "CA1054:URI-like parameters should not be strings", Justification = "test data")]
    public async Task ResolveAsync_WithBuiltInGeometry_ShouldReturnGeometryAsset(string uri)
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result.Should().BeOfType<GeometryAsset>();
        _ = result!.Uri.Should().Be(uri);
        var geometry = (GeometryAsset)result;
        _ = geometry.Lods.Should().ContainSingle();
        _ = geometry.Lods[0].LodIndex.Should().Be(0);
        _ = geometry.Lods[0].SubMeshes.Should().ContainSingle();
        _ = geometry.Lods[0].SubMeshes[0].Name.Should().Be("Main");
    }

    [TestMethod]
    public async Task ResolveAsync_WithDefaultMaterial_ShouldReturnMaterialAsset()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset:///Engine/Generated/Materials/Default";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result.Should().BeOfType<MaterialAsset>();
        _ = result!.Uri.Should().Be(uri);
    }

    [TestMethod]
    public async Task ResolveAsync_WithUnknownUri_ShouldReturnNull()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset:///Engine/Generated/Unknown/Asset";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveAsync_IsCaseSitive()
    {
        // Arrange
        var resolver = new GeneratedAssetResolver();
        const string uri = "asset:///ENGINE/GENERATED/BASICSHAPES/CUBE";

        // Act
        var result = await resolver.ResolveAsync(new Uri(uri)).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeNull();
    }
}
