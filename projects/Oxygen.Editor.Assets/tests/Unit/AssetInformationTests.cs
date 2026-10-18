// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Unit.Tests;

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
        _ = initial.Facts.Should().ContainSingle().Which.Value.Should().Be(asset.SourcePath!.FullPath);
        _ = initial.Description.Should().Contain("No cooked content yet");
        var output = AssetPath.Parse("/Content/Materials/Blue.omat");
        var status = asset.CookStatus! with
        {
            Freshness = AssetCookFreshness.OutOfDate,
            HasPublishedOutput = true,
            OutputAvailability = CookedOutputAvailability.Present,
            Outputs =
            [
                new(asset.Identity, output, ContentCookAssetKind.Material, "Content", output.VirtualPath),
                new(AssetPath.Parse("/Content/Other.omat"), AssetPath.Parse("/Content/Other.omat"), ContentCookAssetKind.Material, "Content", "/Content/Other.omat"),
            ],
        };
        var published = (asset with { CookStatus = status }).Information;
        _ = published.Facts.Should().ContainSingle(fact => fact.Label == "Cooked output").Which.Value.Should().Be(output.VirtualPath);
        _ = published.Description.Should().Contain("Saved inputs have changed").And.Contain("Previously cooked content remains available");
    }

    /// <summary>Engine recipe defaults do not claim that a project has already emitted a cooked copy.</summary>
    [TestMethod]
    public void BuiltinInformationOnlyShowsProvenProjectCopies()
    {
        var builtin = CreateAsset() with
        {
            Identity = AssetPath.Generated.Combine("Materials/Default"),
            DisplayName = "Default",
            PrimaryState = AssetState.Generated,
            Generated = new("Default", "material", AssetPath.Parse("/Content/Materials/OxygenEditor_Default.omat"), GeneratedAssetCategory.Standard),
            SourcePath = null,
            DescriptorPath = null,
            CookStatus = null,
        };
        _ = builtin.Information.Facts.Should().ContainSingle().Which.Should().Be(new AssetInformationFact("Origin", "Oxygen built-in"));
        var copy = builtin with { Identity = AssetPath.Parse("/Content/Materials/OxygenEditor_Default.omat"), BuiltinOrigin = builtin.Identity };
        var grouped = AssetIdentity.GroupBuiltins([builtin, copy]).Single();
        _ = grouped.Information.Facts.Should().ContainSingle(fact => fact.Label == "Project copy").Which.Value.Should().Be(copy.Identity.VirtualPath);
        _ = grouped.Information.Description.Should().Contain("No separate cooking is needed");
    }

    /// <summary>Cooked-only assets expose source unavailability without falsely reporting an authored cook requirement.</summary>
    [TestMethod]
    public void CookedOnlyInformationExplainsReadOnlySourceAndNativeFailure()
    {
        var asset = CreateAsset() with
        {
            PrimaryState = AssetState.Cooked,
            SourcePath = null,
            DescriptorPath = null,
            CookStatus = null,
            Identity = AssetPath.Parse("/Library/Blue.omat"),
            RuntimeAvailability = AssetRuntimeAvailability.Failed,
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
            return new ContentCookedAsset(asset.Identity, AssetPath.Parse(path), ContentCookAssetKind.Material, "Content", path);
        }).ToImmutableArray();
        var information = (asset with { CookStatus = asset.CookStatus! with { HasPublishedOutput = true, Outputs = outputs } }).Information;
        var outputText = information.Facts.Single(fact => string.Equals(fact.Label, "Cooked output", StringComparison.Ordinal)).Value;
        _ = outputText.Split(Environment.NewLine).Should().HaveCount(4);
        _ = outputText.Should().EndWith("+5 more outputs");
    }

    private static AssetEntry CreateAsset()
    {
        var identity = AssetPath.Parse("/Content/Materials/Blue.omat");
        return new(
            identity,
            "Blue",
            AssetKind.Material,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            "/Content/Materials/Blue.omat.json",
            FileSystemPath.From("C:/Project/Content/Materials/Blue.omat.json"),
            FileSystemPath.From("C:/Project/Content/Materials/Blue.omat.json"),
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true)
        {
            CookStatus = new(identity, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], [], []),
        };
    }
}
