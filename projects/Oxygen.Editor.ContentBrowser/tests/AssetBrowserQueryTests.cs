// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks shared asset query semantics without invoking discovery or cooking.</summary>
[TestClass]
public sealed class AssetBrowserQueryTests
{
    /// <summary>Search, type and status intersect; several choices within one group form a union.</summary>
    [TestMethod]
    public void QueryCombinesSearchWithTypeAndStatus()
    {
        var query = new AssetBrowserQuery { SearchText = " blue " };
        Select(query.TypeOptions, "Materials");
        Select(query.TypeOptions, "Geometry");
        Select(query.StatusOptions, "Needs cooking");
        Select(query.StatusOptions, "Out of date");
        _ = query.Matches(CreateAsset("Blue metal", AssetKind.Material, AssetCookFreshness.NeedsCooking)).Should().BeTrue();
        _ = query.Matches(CreateAsset("Blue mesh", AssetKind.Geometry, AssetCookFreshness.OutOfDate)).Should().BeTrue();
        _ = query.Matches(CreateAsset("Blue scene", AssetKind.Scene, AssetCookFreshness.NeedsCooking)).Should().BeFalse();
        _ = query.Matches(CreateAsset("Blue current", AssetKind.Material, AssetCookFreshness.Current)).Should().BeFalse();
        _ = query.Matches(CreateAsset("Red metal", AssetKind.Material, AssetCookFreshness.NeedsCooking)).Should().BeFalse();
        _ = query.FilterLabel.Should().Be("Filter (4)");
    }

    /// <summary>Engine-owned assets never appear as needing a standalone cook.</summary>
    [TestMethod]
    public void BuiltinOwnershipOverridesSyntheticCookFreshness()
    {
        var asset = CreateAsset("Default", AssetKind.Material, AssetCookFreshness.NeedsCooking) with { PrimaryState = AssetState.Generated };
        var query = new AssetBrowserQuery();
        Select(query.StatusOptions, "Needs cooking");
        _ = query.Matches(asset).Should().BeFalse();
        query.ClearFiltersCommand.Execute(parameter: null);
        Select(query.StatusOptions, "Built-in");
        _ = query.Matches(asset).Should().BeTrue();
    }

    /// <summary>Problems include invalid source, damaged output and native failure, independently of an older usable cook.</summary>
    [TestMethod]
    public void ProblemsAndProgressFollowCurrentSharedFacts()
    {
        var query = new AssetBrowserQuery();
        Select(query.StatusOptions, "Problems");
        var current = CreateAsset("Current", AssetKind.Material, AssetCookFreshness.Current);
        _ = query.Matches(current).Should().BeFalse();
        _ = query.Matches(current with { RuntimeAvailability = AssetRuntimeAvailability.Failed }).Should().BeTrue();
        _ = query.Matches(current with { CookStatus = current.CookStatus! with { HasVerifiedOutput = false } }).Should().BeTrue();
        _ = query.Matches(CreateAsset("Missing", AssetKind.Geometry, AssetCookFreshness.MissingSource)).Should().BeTrue();
        query.ClearFiltersCommand.Execute(parameter: null);
        Select(query.StatusOptions, "In progress");
        _ = query.Matches(current with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }).Should().BeTrue();
        _ = query.Matches(current with { RuntimeAvailability = AssetRuntimeAvailability.Updating }).Should().BeTrue();
        _ = query.Matches(current).Should().BeFalse();
    }

    /// <summary>Reset publishes one effective change; clearing filters alone preserves the text query.</summary>
    [TestMethod]
    public void ClearActionsPublishOneChangeAndKeepSearchIntentExplicit()
    {
        var query = new AssetBrowserQuery { SearchText = "blue" };
        Select(query.TypeOptions, "Materials");
        Select(query.StatusOptions, "Needs cooking");
        var changes = 0;
        query.Changed += (_, _) => changes++;
        query.ClearFiltersCommand.Execute(parameter: null);
        _ = changes.Should().Be(1);
        _ = query.SearchText.Should().Be("blue");
        _ = query.FilterCount.Should().Be(0);
        _ = query.IsActive.Should().BeTrue();
        query.ClearAllCommand.Execute(parameter: null);
        _ = changes.Should().Be(2);
        _ = query.IsActive.Should().BeFalse();
        _ = query.SearchText.Should().BeEmpty();
    }

    private static void Select(IReadOnlyList<AssetFilterOption> options, string label)
        => options.Single(option => string.Equals(option.Label, label, StringComparison.Ordinal)).IsSelected = true;

    private static ContentBrowserAssetItem CreateAsset(string name, AssetKind kind, AssetCookFreshness freshness)
    {
        var uri = new Uri("asset:///Content/" + Uri.EscapeDataString(name));
        var current = freshness == AssetCookFreshness.Current;
        return new(
            uri,
            name,
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            uri.AbsolutePath,
            SourcePath: null,
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            CookStatus = new(uri, freshness, HasPublishedOutput: current, HasVerifiedOutput: current, [], [], []),
        };
    }
}
