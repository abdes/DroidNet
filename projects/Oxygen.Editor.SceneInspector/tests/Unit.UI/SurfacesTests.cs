// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// The Surfaces list (instance-overrides-inspector.md 2): one card per surface of the geometry's inventory, each value the
/// node's override or the asset's, edits and resets as undoable overrides, mixed values across nodes that share the
/// geometry, and the changes kept for a surface the geometry no longer has.
/// </summary>
[TestClass]
internal sealed class SurfacesTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly Guid InteriorId = Guid.Parse("10000000-0000-0000-0000-000000000002");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task CardsShowEachSurfaceWithTheAssetValuesUnmarked() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);

        _ = model.Surfaces.Select(static card => card.Name).Should().Equal("Paint", "Interior");
        _ = model.SurfaceCount.Should().Be("2");
        var interior = model.Surfaces[1];
        _ = interior.Visible.Should().BeTrue();
        _ = interior.CastsShadows.Should().BeFalse("the asset turns the interior's shadows off");
        _ = interior.Summary.Should().Be("Geometry default · No shadows");
        _ = interior.OverrideState.Should().Be(OverrideState.None, "an asset value is not a change of this scene");
        _ = interior.CastsShadowsOverrideState.Should().Be(OverrideState.None);
    });

    [TestMethod]
    public Task HidingOneSurfaceOverridesOnlyItAndUndoRemovesTheOverride() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);
        var paint = model.Surfaces[0];

        await model.SetSurfaceFlagAsync(paint, SceneSchema.Geometry.Surface.Visibility.Id, value: false, "Edit Surface Visibility").ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Visibility, paint.Target.Part, out var visible).Should().BeTrue();
        _ = visible.Should().BeFalse();
        _ = fixture.Geometry.Overrides.Parts.Keys.Should().Equal(paint.Target.Part);
        _ = paint.Visible.Should().BeFalse();
        _ = paint.VisibleOverrideState.Should().Be(OverrideState.Full);
        _ = paint.OverrideState.Should().Be(OverrideState.Full);
        _ = paint.Summary.Should().Be("Geometry default · Hidden");
        _ = paint.VisibleResetToolTip.Should().Be("Reset to the asset value (On)");

        await fixture.Authoring.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
    });

    [TestMethod]
    public Task AnOverrideChangeUpdatesTheCardsInPlaceWithoutReadingTheInventory() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);
        var paint = model.Surfaces[0];
        using var reads = fixture.Authoring.Surfaces.HoldReads();

        // As an undo, a redo or another panel changes it: the inspector updates the section's values.
        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.Surface.Visibility, paint.Target.Part, paint.Target.LayoutRevision, value: false);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = model.IsSurfaceLoading.Should().BeFalse("an override never changes the geometry's surfaces");
        _ = model.Surfaces[0].Should().BeSameAs(paint);
        _ = paint.Visible.Should().BeFalse();
        _ = paint.CanEdit.Should().BeTrue();
    });

    [TestMethod]
    public Task ResettingARowRemovesOneOverrideAndResettingTheCardRemovesAllInOneStep() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        var paint = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Geometry.Overrides = fixture.Geometry.Overrides
            .With(SceneSchema.Geometry.Surface.Visibility, paint.Part, paint.LayoutRevision, value: false)
            .With(SceneSchema.Geometry.Surface.CastsShadows, paint.Part, paint.LayoutRevision, value: false)
            .With(SceneSchema.Geometry.Surface.Material, paint.Part, paint.LayoutRevision, fixture.MaterialAsset);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);
        var card = model.Surfaces[0];
        _ = card.ResetToolTip.Should().Be("Reset Material, Visible and Cast shadows to the asset's values");

        await model.ResetSurfaceValueAsync(card, SceneSchema.Geometry.Surface.Visibility.Id, "Reset Surface Visibility").ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.All.Select(static entry => entry.Property).Should().BeEquivalentTo([SceneSchema.Geometry.Surface.CastsShadows.Id, SceneSchema.Geometry.Surface.Material.Id]);
        _ = card.VisibleOverrideState.Should().Be(OverrideState.None);
        _ = card.Visible.Should().BeTrue("the asset's value applies again");

        var steps = fixture.Authoring.Document.History.UndoStack.Count;
        await model.ResetSurfaceAsync(card).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
        _ = fixture.Authoring.Document.History.UndoStack.Should().HaveCount(steps + 1);
        _ = card.OverrideState.Should().Be(OverrideState.None);
    });

    [TestMethod]
    public Task MaterialPickerEditsItsCardsSurfaceAndResetRemovesIt() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        var paint = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, paint.Part, paint.LayoutRevision, AssetPath.Parse("/Content/Materials/Body.omat"));
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);
        var interior = model.Surfaces[1];

        await interior.PrepareMaterialPickerAsync().ConfigureAwait(true);
        await interior.ApplyMaterialAsync(new AssetChoice("Trim", fixture.MaterialAsset, "Material", fixture.MaterialAsset.VirtualPath, IsEnabled: true, Glyph: string.Empty)).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, interior.Target.Part, out var trim).Should().BeTrue();
        _ = trim.Should().Be(fixture.MaterialAsset);
        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, paint.Part, out var body).Should().BeTrue();
        _ = body.Should().Be(AssetPath.Parse("/Content/Materials/Body.omat"));
        _ = interior.MaterialOverrideState.Should().Be(OverrideState.Full);

        await model.ResetSurfaceValueAsync(interior, SceneSchema.Geometry.Surface.Material.Id, "Reset Surface Material").ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.All.Should().ContainSingle().Which.Part.Should().Be(paint.Part);
        _ = interior.MaterialLabel.Should().Be("Geometry default");
    });

    /// <summary>
    /// A model's material is not listed in the Content Explorer, and is still offered by the material picker: it is
    /// reached by its identity under the model.
    /// </summary>
    /// <returns>The asynchronous picker test.</returns>
    [TestMethod]
    public Task MaterialPickerListsModelMaterials() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        var paint = AssetPath.Parse("/Content/Models/Ship/Ship.glb/Paint.omat");
        var material = await fixture.Assets.Object.ResolveAsync(fixture.MaterialAsset, CancellationToken.None).ConfigureAwait(true);
        var geometry = await fixture.Assets.Object.ResolveAsync(fixture.GeometryAsset, CancellationToken.None).ConfigureAwait(true);
        var product = material! with { Identity = paint, DisplayName = "Paint", CookStatus = material.CookStatus! with { AssetUri = paint } };
        _ = fixture.Assets.SetupGet(value => value.Items).Returns(System.Reactive.Linq.Observable.Return<IReadOnlyList<Oxygen.Editor.Assets.AssetEntry>>([geometry!, material, product]));
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);

        await model.Surfaces.Single().PrepareMaterialPickerAsync().ConfigureAwait(true);

        _ = model.MaterialChoices.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Contain(paint);
    });

    [TestMethod]
    public Task NodesThatDisagreeShowAMixedPartlyOverriddenValueThatSetsOnAll() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        var paint = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.Surface.Visibility, paint.Part, paint.LayoutRevision, value: false);
        var second = new SceneNode(fixture.Authoring.Scene, "Second");
        var secondGeometry = new GeometryComponent { Name = "Geometry", Geometry = fixture.Geometry.Geometry };
        _ = second.AddComponent(secondGeometry);
        fixture.Authoring.Scene.RootNodes.Add(second);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        fixture.Authoring.Select(fixture.Authoring.Node, second);
        await WaitForRenderAsync().ConfigureAwait(true);
        var model = await OpenAsync(host).ConfigureAwait(true);
        var card = model.Surfaces.Single();

        _ = card.Visible.Should().BeNull();
        _ = card.VisibleOverrideState.Should().Be(OverrideState.Partial);
        _ = card.Summary.Should().Be("Geometry default · Hidden on 1");
        _ = card.VisibleResetToolTip.Should().Be("Reset to the asset value (On), on the 1 of 2 nodes that change it");

        await model.SetSurfaceFlagAsync(card, SceneSchema.Geometry.Surface.Visibility.Id, value: true, "Edit Surface Visibility").ConfigureAwait(true);

        _ = card.Visible.Should().BeTrue();
        _ = card.VisibleOverrideState.Should().Be(OverrideState.Full, "setting a value writes it on every selected node");
        _ = secondGeometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Visibility, paint.Part, out var shown).Should().BeTrue();
        _ = shown.Should().BeTrue();
    });

    [TestMethod]
    public Task NodesWithDifferentGeometriesShowNoSurfaces() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        var second = new SceneNode(fixture.Authoring.Scene, "Second");
        _ = second.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Sphere") });
        fixture.Authoring.Scene.RootNodes.Add(second);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        fixture.Authoring.Select(fixture.Authoring.Node, second);
        await WaitForRenderAsync().ConfigureAwait(true);
        var model = await OpenAsync(host).ConfigureAwait(true);

        _ = model.Surfaces.Should().BeEmpty();
        _ = model.SurfaceNotice.Should().Be("Select nodes that share one geometry.");
    });

    [TestMethod]
    public Task ChangesKeptForALostSurfaceApplyToAChosenSurfaceOrAreRemoved() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishPaintAndInterior(fixture);
        var paint = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        var lost = new PartId(SceneSchema.Geometry.Surface.Kind, Guid.Parse("40000000-0000-0000-0000-000000000004"));
        fixture.Geometry.Overrides = fixture.Geometry.Overrides
            .With(SceneSchema.Geometry.Surface.Visibility, lost, paint.LayoutRevision, value: false)
            .With(SceneSchema.Geometry.Surface.Material, lost, paint.LayoutRevision, fixture.MaterialAsset);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);

        var kept = model.KeptChanges.Should().ContainSingle().Subject;
        _ = model.HasKeptChanges.Should().BeTrue();
        _ = kept.Name.Should().Be("Surface 40000000");
        _ = kept.Summary.Should().Contain("Visible Off");
        _ = kept.Targets.Should().Equal(model.Surfaces);

        await model.RetargetKeptChangesAsync(kept, model.Surfaces[1]).ConfigureAwait(true);

        _ = model.KeptChanges.Should().BeEmpty();
        _ = fixture.Geometry.Overrides.Parts.Keys.Should().Equal(model.Surfaces[1].Target.Part);
        _ = model.Surfaces[1].Visible.Should().BeFalse();

        await fixture.Authoring.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        model.UpdateValues([fixture.Authoring.Node]);
        await model.RemoveKeptChangesAsync(model.KeptChanges.Single()).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
        _ = model.KeptChanges.Should().BeEmpty();
    });

    /// <summary>Unavailable metadata shows no surface instead of manufacturing one.</summary>
    /// <returns>The asynchronous unavailable-state regression.</returns>
    [TestMethod]
    public Task UnavailableInventoryShowsNoSurface() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        fixture.Authoring.Surfaces.Remove(fixture.Geometry.Geometry);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = await OpenAsync(host).ConfigureAwait(true);

        _ = model.Surfaces.Should().BeEmpty();
        _ = model.IsSurfaceInventoryFailed.Should().BeTrue();
        _ = model.SurfaceNotice.Should().Contain("unavailable");
    });

    private static async Task<GeometryViewModel> OpenAsync(SceneInspectorViewModel host)
    {
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshSurfacesAsync().ConfigureAwait(true);
        return model;
    }

    // Paint is visible and casts shadows in the asset; Interior is visible and casts none.
    private static void PublishPaintAndInterior(GeometryFixture fixture)
    {
        var paint = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Authoring.Surfaces.Publish(new GeometrySurfaceMetadata(
            fixture.Geometry.Geometry,
            Guid.Parse("20000000-0000-0000-0000-000000000001"),
            paint.LayoutRevision,
            [
                new(paint.SurfaceId, "Paint", Visible: true, CastsShadows: true, [new(0, 0, Guid.Empty)]),
                new(InteriorId, "Interior", Visible: true, CastsShadows: false, [new(0, 1, Guid.Empty), new(1, 2, Guid.Empty)]),
            ],
            SceneSchema.Geometry.Defaults.LodPolicy));
    }
}
