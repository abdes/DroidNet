// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets.Materials;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Checks what a property that references assets is offered, and that its current assignment stays visible.</summary>
[TestClass]
public sealed class AssetRuleTests
{
    private static readonly AssetPath Wood = AssetPath.Parse("/Content/Materials/Wood.omat");
    private static readonly AssetPath Stone = AssetPath.Parse("/Content/Materials/Stone.omat");

    [TestMethod]
    public void ARuleOffersOnlyItsKindByName()
    {
        var assets = new[]
        {
            Entry(Wood),
            Entry(AssetPath.Parse("/Content/Geometry/Crate.ogeo"), kind: AssetKind.Geometry),
            Entry(Stone),
            Entry(AssetPath.Parse("/Content/Textures/Sky.otex"), kind: AssetKind.Texture),
        };

        _ = AssetRule.For(AssetKind.Material).Apply(assets).Select(static asset => asset.Identity).Should().Equal(Stone, Wood);
    }

    [TestMethod]
    [DataRow(AssetKind.Material, "Content/Materials/Wood.omat")]
    [DataRow(AssetKind.Geometry, "Content/Geometry/Crate.ogeo")]
    [DataRow(AssetKind.Scene, "Content/Scenes/Main.oscene")]
    [DataRow(AssetKind.Texture, "Content/Textures/Sky.otex")]
    [DataRow(AssetKind.Script, "Content/Scripts/Door.oscript")]
    [DataRow(AssetKind.InputAction, "Content/Input/Jump.oiact")]
    [DataRow(AssetKind.InputMappingContext, "Content/Input/Player.oimap")]
    [DataRow(AssetKind.PhysicsScene, "Content/Scenes/Main.opscene")]
    public void EveryKindThePipelineCooksCanBeReferenced(AssetKind kind, string path)
    {
        var asset = Entry(AssetPath.Parse("/" + path), kind: kind);

        _ = AssetRule.For(kind).Accepts(asset).Should().BeTrue();
    }

    [TestMethod]
    public void AKindThePipelineDoesNotCookAcceptsNothing()
    {
        var image = Entry(AssetPath.Parse("/Content/Images/Sky.png"), AssetState.Source, AssetKind.Image);

        _ = AssetRule.For(AssetKind.Image).Apply([image], image.Identity).Should().BeEmpty();
    }

    [TestMethod]
    public void MissingOrBrokenAssetsAreNotOffered()
    {
        var assets = new[] { Entry(Wood), Entry(Stone, AssetState.Broken, selectable: false) };

        _ = AssetRule.For(AssetKind.Material).Apply(assets).Should().ContainSingle().Which.Identity.Should().Be(Wood);
    }

    [TestMethod]
    public void TheCurrentAssignmentIsKeptWhenItsFileIsBrokenOrUnknown()
    {
        var gone = AssetPath.Parse("/Content/Materials/Gone.omat");
        var assets = new[] { Entry(Wood), Entry(Stone, AssetState.Broken, selectable: false) };

        var offered = AssetRule.For(AssetKind.Material).Apply(assets, Stone, gone, null);

        _ = offered.Select(static asset => asset.Identity).Should().BeEquivalentTo([Wood, Stone, gone]);
        _ = offered.Single(asset => asset.Identity == Stone).PrimaryState.Should().Be(AssetState.Broken);
        var missing = offered.Single(asset => asset.Identity == gone);
        _ = missing.PrimaryState.Should().Be(AssetState.Missing);
        _ = missing.IsSelectable.Should().BeFalse();
        _ = missing.DisplayName.Should().Be("Gone");
    }

    [TestMethod]
    public void AKeptAssignmentNeverAddsAnotherKindOrAnInternalEngineAsset()
    {
        var crate = AssetPath.Parse("/Content/Geometry/Crate.ogeo");
        var internalMaterial = Entry(EngineAssets.Internal.Combine("Materials/Gizmo.omat"), AssetState.Packaged);
        var assets = new[] { Entry(Wood), Entry(crate, kind: AssetKind.Geometry), internalMaterial };

        _ = AssetRule.For(AssetKind.Material).Apply(assets, crate, internalMaterial.Identity).Should().ContainSingle().Which.Identity.Should().Be(Wood);
    }

    [TestMethod]
    public void EngineInternalAssetsAreNotOfferedButGeneratedOnesAre()
    {
        var generated = Entry(EngineAssets.DefaultMaterial, AssetState.Packaged);
        var internalMaterial = Entry(EngineAssets.Internal.Combine("Materials/Gizmo.omat"), AssetState.Packaged);

        _ = AssetRule.For(AssetKind.Material).Apply([Entry(Wood), generated, internalMaterial]).Select(static asset => asset.Identity)
            .Should().BeEquivalentTo([Wood, generated.Identity]);
    }
    [TestMethod]
    public void ACookedReferenceIsOfferedOnlyAssetsWithCookedOutput()
    {
        var cooked = Entry(Wood) with { CookStatus = Status(Wood, available: true) };
        var uncooked = Entry(Stone) with { CookStatus = Status(Stone, available: false) };
        var rule = AssetRule.ForCooked(AssetKind.Material);

        _ = rule.Apply([cooked, uncooked]).Should().ContainSingle().Which.Identity.Should().Be(Wood);
        _ = rule.Accepts(uncooked).Should().BeFalse();
    }

    [TestMethod]
    public void TheFeedPublishesWhatTheRuleAcceptsEachTimeItChanges()
    {
        using var items = new BehaviorSubject<IReadOnlyList<AssetEntry>>([Entry(Wood)]);
        var feed = Mock.Of<IAssetFeed>(value => value.Items == items);
        IReadOnlyList<AssetEntry> offered = [];
        using var subscription = feed.Assets(AssetRule.For(AssetKind.Material), Stone).Subscribe(value => offered = value);
        _ = offered.Select(static asset => asset.Identity).Should().BeEquivalentTo([Wood, Stone], "the assignment is kept while no catalog knows it");
        _ = offered.Single(asset => asset.Identity == Stone).PrimaryState.Should().Be(AssetState.Missing);

        items.OnNext([Entry(Wood), Entry(Stone)]);

        _ = offered.Should().OnlyContain(static asset => asset.IsSelectable);
    }

    [TestMethod]
    public void AMaterialPreviewReadsItsDescriptorsBaseColor()
    {
        using var workspace = new AssetWorkspace();
        var path = workspace.SourcePath("Content/Materials/Wood.omat.json");
        WriteMaterial(path, 0.2f, 0.3f, 0.4f, 1.0f);

        var preview = MaterialPreviewColor.TryRead(Entry(Wood) with { DescriptorPath = FileSystemPath.From(path) });

        _ = preview.Should().Be(new MaterialPreviewColor(0.2f, 0.3f, 0.4f, 1.0f));
        _ = MaterialPreviewColor.TryRead(Entry(Stone, AssetState.Broken, selectable: false) with { DescriptorPath = FileSystemPath.From(path) }).Should().BeNull();
    }

    private static AssetEntry Entry(AssetPath identity, AssetState state = AssetState.Descriptor, AssetKind kind = AssetKind.Material, bool selectable = true)
        => new(
            Identity: identity,
            DisplayName: identity.Name.Split('.')[0],
            Kind: kind,
            PrimaryState: state,
            DerivedState: null,
            RuntimeAvailability: AssetRuntimeAvailability.Unknown,
            DisplayPath: identity.VirtualPath,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: selectable);

    private static AssetCookStatus Status(AssetPath identity, bool available)
        => new(identity, available ? AssetCookFreshness.Current : AssetCookFreshness.NeedsCooking, available, available ? CookedOutputAvailability.Present : CookedOutputAvailability.Unknown, [], [], []);

    private static void WriteMaterial(string path, float r, float g, float b, float a)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "Wood",
                new MaterialPbrMetallicRoughness(r, g, b, a, metallicFactor: 0.0f, roughnessFactor: 0.5f),
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }
}
