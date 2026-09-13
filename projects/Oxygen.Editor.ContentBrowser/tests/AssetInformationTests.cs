// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks informational relationships without inferring publication from an engine recipe or an asset name.</summary>
[TestClass]
public sealed class AssetInformationTests
{
    /// <summary>First cook and a later stale source keep the actual source and published output distinguishable.</summary>
    [TestMethod]
    public void AuthoredInformationTracksActualPublicationAndIgnoresDependencyOutputs()
    {
        var asset = CreateAsset();
        var initial = asset.Information;
        _ = initial.Title.Should().Be("Blue (Material)");
        _ = initial.Location.Should().Be(asset.DisplayPath);
        _ = initial.Facts.Should().ContainSingle().Which.Value.Should().Be(asset.SourcePath);
        _ = initial.Description.Should().Contain("No cooked content yet");
        var output = new Uri("asset:///Content/Materials/Blue.omat");
        var status = asset.CookStatus! with
        {
            Freshness = AssetCookFreshness.OutOfDate, HasPublishedOutput = true, HasVerifiedOutput = true,
            Outputs =
            [
                new(asset.IdentityUri, output, ContentCookAssetKind.Material, "Content", output.AbsolutePath),
                new(new("asset:///Content/Other.omat.json"), new("asset:///Content/Other.omat"), ContentCookAssetKind.Material, "Content", "/Content/Other.omat"),
            ],
        };
        var published = (asset with { CookStatus = status, CookedUri = output }).Information;
        _ = published.Facts.Should().ContainSingle(fact => fact.Label == "Cooked output").Which.Value.Should().Be(output.AbsolutePath);
        _ = published.Description.Should().Contain("Saved inputs have changed").And.Contain("Previously cooked content remains available");
    }

    /// <summary>Engine recipe defaults do not claim that a project has already emitted a cooked copy.</summary>
    [TestMethod]
    public void BuiltinInformationOnlyShowsProvenProjectCopies()
    {
        var builtin = CreateAsset() with
        {
            IdentityUri = AssetUris.BuildGeneratedUri("Materials/Default"), DisplayName = "Default", PrimaryState = AssetState.Generated,
            Generated = new("Default", "material", "/Content/Materials/OxygenEditor_Default.omat"),
            SourcePath = null, DescriptorPath = null, CookStatus = null,
        };
        _ = builtin.Information.Facts.Should().ContainSingle().Which.Should().Be(new AssetInformationFact("Origin", "Oxygen built-in"));
        var copy = builtin with { IdentityUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"), CookedUri = new("asset:///Content/Materials/OxygenEditor_Default.omat"), BuiltinOriginUri = builtin.IdentityUri };
        var grouped = AssetIdentityGrouping.GroupBuiltins([builtin, copy]).Single();
        _ = grouped.Information.Facts.Should().ContainSingle(fact => fact.Label == "Project copy").Which.Value.Should().Be(copy.IdentityUri.AbsolutePath);
        _ = grouped.Information.Description.Should().Contain("No separate cooking is needed");
    }

    /// <summary>Cooked-only assets expose source unavailability without falsely reporting an authored cook requirement.</summary>
    [TestMethod]
    public void CookedOnlyInformationExplainsReadOnlySourceAndNativeFailure()
    {
        var asset = CreateAsset() with
        {
            PrimaryState = AssetState.Cooked, SourcePath = null, DescriptorPath = null, CookStatus = null,
            CookedUri = new("asset:///Library/Blue.omat"), RuntimeAvailability = AssetRuntimeAvailability.Failed,
            RuntimeReason = "The cooked material could not be loaded.",
        };
        _ = asset.Information.Facts.Should().Contain(new AssetInformationFact("Source", "Unavailable · read-only"));
        _ = asset.Information.Description.Should().Contain("read-only").And.Contain(asset.RuntimeReason);
    }

    /// <summary>A tooltip bounds a large output list while retaining the total undisplayed count.</summary>
    [TestMethod]
    public void ManyOutputsKeepTheTooltipCompact()
    {
        var asset = CreateAsset();
        var outputs = Enumerable.Range(0, 8).Select(index =>
        {
            var path = string.Create(CultureInfo.InvariantCulture, $"/Content/Blue{index}.omat");
            return new ContentCookedAsset(asset.IdentityUri, new("asset://" + path), ContentCookAssetKind.Material, "Content", path);
        }).ToImmutableArray();
        var information = (asset with { CookStatus = asset.CookStatus! with { HasPublishedOutput = true, Outputs = outputs } }).Information;
        var outputText = information.Facts.Single(fact => string.Equals(fact.Label, "Cooked output", StringComparison.Ordinal)).Value;
        _ = outputText.Split(Environment.NewLine).Should().HaveCount(4);
        _ = outputText.Should().EndWith("+5 more outputs");
    }

    private static ContentBrowserAssetItem CreateAsset()
    {
        var uri = new Uri("asset:///Content/Materials/Blue.omat.json");
        return new(
            uri,
            "Blue",
            AssetKind.Material,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            uri.AbsolutePath,
            "C:/Project/Content/Materials/Blue.omat.json",
            "C:/Project/Content/Materials/Blue.omat.json",
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            CookStatus = new(uri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, HasVerifiedOutput: false, [], [], []),
        };
    }
}
