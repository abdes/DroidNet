// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

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

    /// <summary>A packaged engine asset is found by the Engine status and never by a cook status.</summary>
    [TestMethod]
    public void EnginePackagedAssetsMatchOnlyTheEngineStatus()
    {
        var asset = CreateAsset("Default", AssetKind.Material, AssetCookFreshness.NeedsCooking) with { Identity = EngineAssets.DefaultMaterial, PrimaryState = AssetState.Packaged, CookStatus = null };
        var query = new AssetBrowserQuery();
        Select(query.StatusOptions, "Needs cooking");
        _ = query.Matches(asset).Should().BeFalse();
        query.ClearFiltersCommand.Execute(parameter: null);
        Select(query.StatusOptions, "Engine");
        _ = query.Matches(asset).Should().BeTrue();
        _ = query.Matches(CreateAsset("Blue", AssetKind.Material, AssetCookFreshness.Current)).Should().BeFalse();
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
        _ = query.Matches(current with { CookStatus = current.CookStatus! with { OutputAvailability = CookedOutputAvailability.Missing } }).Should().BeTrue();
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

    /// <summary>A model source with no import settings is found by the not-imported status.</summary>
    [TestMethod]
    public void NotImportedStatusFindsModelSourcesWithoutImportSettings()
    {
        var model = CreateAsset("model.gltf", AssetKind.ForeignSource, AssetCookFreshness.Current) with { PrimaryState = AssetState.Source, CookStatus = null };
        var query = new AssetBrowserQuery();
        Select(query.StatusOptions, "Not imported");
        _ = query.Matches(model).Should().BeTrue();
        _ = query.Matches(model with { ImportSource = model.Identity }).Should().BeFalse();
    }

    private static void Select(IReadOnlyList<AssetFilterOption> options, string label)
        => options.Single(option => string.Equals(option.Label, label, StringComparison.Ordinal)).IsSelected = true;

    private static AssetEntry CreateAsset(string name, AssetKind kind, AssetCookFreshness freshness)
    {
        var identity = AssetPath.Create("Content", name);
        var current = freshness == AssetCookFreshness.Current;
        return new(
            identity,
            name,
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            identity.VirtualPath,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            CookStatus = new(identity, freshness, HasPublishedOutput: current, OutputAvailability: current ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [], [], []),
        };
    }
}
