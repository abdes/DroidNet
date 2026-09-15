// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks provenance-based grouping independently of asset names and scene use counts.</summary>
[TestClass]
public sealed class AssetIdentityGroupingTests
{
    /// <summary>A built-in and its proven copy form one logical row, while same-named independent materials stay separate.</summary>
    [TestMethod]
    public void DefaultGroupsOnlyItsVerifiedCompanion()
    {
        var origin = CreateOrigin();
        var copy = CreateCopy(origin);
        var authored = copy with { IdentityUri = new("asset:///Content/Materials/Default.omat.json"), SourcePath = "Default.omat.json", DescriptorPath = "Default.omat.json", Generated = null, BuiltinOriginUri = null, PrimaryState = AssetState.Descriptor };
        var unproven = copy with { IdentityUri = new("asset:///Content/Materials/AnotherDefault.omat"), Generated = null, BuiltinOriginUri = null, PrimaryState = AssetState.Cooked };
        var rows = AssetIdentityGrouping.GroupBuiltins([copy, authored, origin, unproven]);
        _ = rows.Should().HaveCount(3);
        var builtin = rows.Single(static item => item.IsBuiltin);
        _ = builtin.IdentityUri.Should().Be(origin.IdentityUri);
        _ = builtin.CookedCompanions.Should().ContainSingle().Which.IdentityUri.Should().Be(copy.IdentityUri);
        _ = AssetIdentityGrouping.Represents(builtin, copy.IdentityUri).Should().BeTrue();
        _ = rows.Should().Contain(authored).And.Contain(unproven);
    }

    /// <summary>A Cooked-only scope retains its published URI and availability.</summary>
    [TestMethod]
    public void ScopedCookedCopyRetainsItsExistingReference()
    {
        var copy = CreateCopy(CreateOrigin());
        _ = AssetIdentityGrouping.GroupBuiltins([copy]).Should().ContainSingle().Which.Should().BeSameAs(copy);
    }

    /// <summary>A failure on the cooked representation stays visible on the grouped built-in.</summary>
    [TestMethod]
    public void GroupRetainsCompanionFailureAndSearchablePath()
    {
        var origin = CreateOrigin();
        var copy = CreateCopy(origin) with { RuntimeAvailability = AssetRuntimeAvailability.Failed, RuntimeReason = "Cooked copy failed to load.", DiagnosticCodes = ["LOAD_FAILED"] };
        var row = AssetIdentityGrouping.GroupBuiltins([origin, copy]).Single();
        _ = row.RuntimeAvailability.Should().Be(AssetRuntimeAvailability.Failed);
        _ = row.RuntimeReason.Should().Be(copy.RuntimeReason);
        _ = row.DiagnosticCodes.Should().Contain("LOAD_FAILED");
        _ = new AssetBrowserQuery { SearchText = "OxygenEditor_Default" }.Matches(row).Should().BeTrue();
    }

    /// <summary>Distinct canonical geometry identities remain independent choices.</summary>
    [TestMethod]
    public void CanonicalGeneratorsKeepDistinctChoices()
    {
        var ico = CreateOrigin() with { IdentityUri = AssetUris.BuildGeneratedUri("BasicShapes/IcoSphere"), Kind = AssetKind.Geometry, DisplayName = "IcoSphere", Generated = new("IcoSphere", "geometry", "/Content/IcoSphere.ogeo", GeneratedAssetCategory.Standard) };
        var sphere = ico with { IdentityUri = AssetUris.BuildGeneratedUri("BasicShapes/Sphere"), DisplayName = "Sphere", Generated = new("Sphere", "geometry", "/Content/Sphere.ogeo", GeneratedAssetCategory.Standard) };
        _ = AssetIdentityGrouping.GroupBuiltins([ico, sphere]).Should().Equal(ico, sphere);
    }

    private static ContentBrowserAssetItem CreateOrigin()
        => new(
            AssetUris.BuildGeneratedUri("Materials/Default"),
            "Default",
            AssetKind.Material,
            AssetState.Generated,
            DerivedState: null,
            AssetRuntimeAvailability.Mounted,
            "/Engine/Generated/Materials/Default",
            SourcePath: null,
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            Generated = new GeneratedAssetMetadata("Default", "oxygen.material-descriptor.v1", "/Content/Materials/OxygenEditor_Default.omat", GeneratedAssetCategory.Standard),
        };

    private static ContentBrowserAssetItem CreateCopy(ContentBrowserAssetItem origin)
        => origin with
        {
            IdentityUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"),
            BuiltinOriginUri = origin.IdentityUri,
            DisplayPath = "/Content/Materials/OxygenEditor_Default.omat",
            CookedUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"),
            CookedPath = "C:/Vortex/.cooked/Content/Materials/OxygenEditor_Default.omat",
        };
}
