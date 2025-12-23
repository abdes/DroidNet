// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Assets.Tests;

/// <summary>
/// Unit tests for the <see cref="AssetReference{T}"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class AssetReferenceTests
{
    [TestMethod]
    public void Uri_WhenSet_ShouldNotifyPropertyChanged()
    {
        // Arrange
        var reference = new AssetReference<GeometryAsset>("asset://OldUri");
        var propertyChangedFired = false;
        reference.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(AssetReference<>.Uri), StringComparison.Ordinal))
            {
                propertyChangedFired = true;
            }
        };

        // Act
        reference.Uri = new("asset://Generated/BasicShapes/Cube");

        // Assert
        _ = propertyChangedFired.Should().BeTrue();
    }

    [TestMethod]
    public void Uri_WhenSetToSameValue_ShouldNotNotify()
    {
        // Arrange
        var reference = new AssetReference<GeometryAsset>("asset://Test");
        var propertyChangedFireCount = 0;
        reference.PropertyChanged += (_, _) => propertyChangedFireCount++;

        // Act
        reference.Uri = new("asset://Test");

        // Assert
        _ = propertyChangedFireCount.Should().Be(0);
    }

    [TestMethod]
    public void Uri_WhenChanged_ShouldInvalidateAsset()
    {
        // Arrange
        var asset = new GeometryAsset
        {
            Uri = new("asset://Generated/BasicShapes/Cube"),
            Lods = [],
        };

        // Act
        var reference = new AssetReference<GeometryAsset>(asset.Uri)
        {
            Asset = asset,
            Uri = new("asset://Generated/BasicShapes/Sphere"),
        };

        // Assert
        _ = reference.Asset.Should().BeNull("changing URI should invalidate the cached asset");
    }

    [TestMethod]
    public void Uri_WhenChangedToMatchAsset_ShouldNotInvalidateAsset()
    {
        // Arrange
        var asset = new GeometryAsset
        {
            Uri = new("asset://Generated/BasicShapes/Cube"),
            Lods = [],
        };

        // Act
        var reference = new AssetReference<GeometryAsset>("asset://Generated/BasicShapes/Sphere")
        {
            Asset = asset,
            Uri = asset.Uri,
        };

        // Assert
        _ = reference.Asset.Should().Be(asset, "URI matches the asset's URI");
    }

    [TestMethod]
    public void Asset_WhenSet_ShouldSyncUri()
    {
        // Arrange
        var reference = new AssetReference<GeometryAsset>("asset://OldUri");
        var asset = new GeometryAsset
        {
            Uri = new("asset://Generated/BasicShapes/Cube"),
            Lods = [],
        };

        // Act
        reference.Asset = asset;

        // Assert
        _ = reference.Uri.Should().Be(asset.Uri, "setting Asset should synchronize Uri");
    }

    [TestMethod]
    public void Asset_WhenSetToNull_ShouldNotChangeUri()
    {
        // Arrange
        var asset = new GeometryAsset
        {
            Uri = new("asset://Generated/BasicShapes/Cube"),
            Lods = [],
        };
        var reference = new AssetReference<GeometryAsset>(asset.Uri) { Asset = asset };

        // Act
        reference.Asset = null;

        // Assert
        _ = reference.Uri.Should().Be(asset.Uri, "clearing Asset should preserve Uri");
    }

    [TestMethod]
    public void Asset_WhenUriMatchesAssetUri_ShouldNotTriggerSync()
    {
        // Arrange
        var asset = new GeometryAsset
        {
            Uri = new("asset://Generated/BasicShapes/Cube"),
            Lods = [],
        };
        var reference = new AssetReference<GeometryAsset>(asset.Uri);
        var uriChangedCount = 0;
        reference.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(AssetReference<>.Uri), StringComparison.Ordinal))
            {
                uriChangedCount++;
            }
        };

        // Act
        reference.Asset = asset;

        // Assert
        _ = uriChangedCount.Should().Be(0, "Uri already matches, no sync needed");
    }
}
