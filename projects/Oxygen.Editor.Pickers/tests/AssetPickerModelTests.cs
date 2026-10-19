// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Pickers.Tests;

[TestClass]
public sealed class AssetPickerModelTests
{
    private static readonly AssetEntry EngineCube = CreateEntry("/Engine/BasicShapes/Cube.ogeo", "Cube", AssetState.Packaged);
    private static readonly AssetEntry PackageRock = CreateEntry("/Props/Geometry/Rock.ogeo", "Rock", AssetState.Packaged);
    private static readonly AssetEntry ContentChair = CreateEntry("/Content/Geometry/Chair.ogeo", "Chair", AssetState.Descriptor);

    [TestMethod]
    public void GroupsFollowEngineThenPackagesThenContent()
    {
        var model = CreateModel();

        model.Update([ContentChair, PackageRock, EngineCube]);

        _ = model.Groups.Select(static group => group.Origin).Should().Equal(AssetOrigin.Engine, AssetOrigin.Package, AssetOrigin.Content);
        _ = model.Groups.Select(static group => group.Rows.Single().Choice.Asset).Should().Equal(EngineCube.Identity, PackageRock.Identity, ContentChair.Identity);
    }

    [TestMethod]
    public void GroupsWithoutChoicesAreHidden()
    {
        var model = CreateModel();
        model.Update([ContentChair, PackageRock, EngineCube]);

        model.Update([EngineCube, ContentChair]);

        _ = model.Groups.Select(static group => group.Origin).Should().Equal(AssetOrigin.Engine, AssetOrigin.Content);
        _ = model.Find(PackageRock.Identity).Should().BeNull();
    }

    [TestMethod]
    public void SearchMatchesNamesAndLocationsIgnoringCase()
    {
        var model = CreateModel();
        model.Update([ContentChair, PackageRock, EngineCube]);

        model.SearchText = "basicshapes";
        _ = model.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Equal(EngineCube.Identity);

        model.SearchText = "ROCK";
        _ = model.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Equal(PackageRock.Identity);

        model.SearchText = "lamp";
        _ = model.IsEmpty.Should().BeTrue();

        model.SearchText = string.Empty;
        _ = model.Groups.Should().HaveCount(3);
    }

    [TestMethod]
    public void UpdatesKeepRowsOfTheSameAsset()
    {
        var model = CreateModel();
        model.Update([ContentChair]);
        var row = model.Groups.Single().Rows.Single();

        model.Update([ContentChair with { IsSelectable = false }]);

        _ = model.Groups.Single().Rows.Single().Should().BeSameAs(row);
        _ = row.Choice.IsEnabled.Should().BeFalse();
    }

    [TestMethod]
    public void AnAssetIsOfferedOnce()
    {
        var model = CreateModel();

        model.Update([ContentChair, ContentChair with { DisplayName = "Chair copy" }]);

        _ = model.Groups.Single().Rows.Should().ContainSingle().Which.Choice.Name.Should().Be("Chair");
    }

    [TestMethod]
    public void TheClearingChoiceAssignsNoAsset()
    {
        var clear = new AssetChoice("None", Asset: null, "No geometry", "No geometry", IsEnabled: true, Glyph: string.Empty);

        _ = new AssetPickerModel(Describe, clear).HasClear.Should().BeTrue();
        _ = new Action(() => _ = new AssetPickerModel(Describe, clear with { Asset = ContentChair.Identity })).Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void ARowMustAssignTheAssetItDescribes()
    {
        var model = new AssetPickerModel(static asset => AssetChoice.For(asset, "Geometry", string.Empty) with { Asset = EngineCube.Identity });

        _ = new Action(() => model.Update([ContentChair])).Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void ChoicesDescribeTypeStatusAndSelectability()
    {
        var choice = AssetChoice.For(ContentChair with { IsSelectable = false }, "Geometry", "G");

        _ = choice.Name.Should().Be("Chair");
        _ = choice.Asset.Should().Be(ContentChair.Identity);
        _ = choice.DisplayType.Should().StartWith("Geometry · ");
        _ = choice.DisplayPath.Should().Be(ContentChair.DisplayPath);
        _ = choice.IsEnabled.Should().BeFalse();
        _ = choice.Glyph.Should().Be("G");
    }

    private static AssetPickerModel CreateModel() => new(Describe);

    private static AssetChoice Describe(AssetEntry asset) => AssetChoice.For(asset, "Geometry", string.Empty);

    private static AssetEntry CreateEntry(string virtualPath, string name, AssetState state)
    {
        var asset = AssetPath.Parse(virtualPath);
        return new(asset, name, AssetKind.Geometry, state, DerivedState: null, AssetRuntimeAvailability.Unknown, asset.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true);
    }
}
