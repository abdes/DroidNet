// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks which URIs name an asset.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(AssetUris))]
public class AssetUrisTests
{
    [TestMethod]
    [DataRow("asset:///Content/Geometry/Crate.ogeo")]
    [DataRow("asset:///Engine/Generated/BasicShapes/Cube")]
    [DataRow("ASSET:///Content/Materials/Red.omat.json")]
    public void EnsureAssetUri_AssetUri_ReturnsIt(string value)
    {
        var uri = new Uri(value);

        var result = AssetUris.EnsureAssetUri(uri);

        _ = result.Should().BeSameAs(uri);
    }

    [TestMethod]
    [DataRow("https://example.com/Content/Crate.ogeo")]
    [DataRow("file:///C:/Content/Crate.ogeo")]
    [DataRow("asset:///")]
    public void EnsureAssetUri_NotAnAssetUri_Throws(string value)
    {
        var uri = new Uri(value);

        var act = () => AssetUris.EnsureAssetUri(uri);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void EnsureAssetUri_RelativeUri_Throws()
    {
        var uri = new Uri("Content/Crate.ogeo", UriKind.Relative);

        var act = () => AssetUris.EnsureAssetUri(uri);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void EnsureAssetUri_Null_Throws()
    {
        var act = () => AssetUris.EnsureAssetUri(null!);

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void IsAssetUri_Null_IsFalse()
        => _ = AssetUris.IsAssetUri(null).Should().BeFalse();
}
