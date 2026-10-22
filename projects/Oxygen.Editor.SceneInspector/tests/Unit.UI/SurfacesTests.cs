// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

[TestClass]
internal sealed class SurfacesTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The surface selector applies a nonzero surface without touching another surface's override.</summary>
    /// <returns>The asynchronous Inspector regression.</returns>
    [TestMethod]
    public Task MaterialPickerEditsSelectedNativeSurfaceAndClearRemovesIt() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        var geometry = fixture.Geometry.Geometry;
        var first = SceneAuthoringFixture.TargetFor(geometry);
        var second = first with
        {
            SurfaceId = Guid.Parse("10000000-0000-0000-0000-000000000002"),
        };
        fixture.Authoring.Surfaces.Publish(new GeometrySurfaceMetadata(geometry, Guid.Parse("20000000-0000-0000-0000-000000000001"), first.LayoutRevision, [new(first.SurfaceId, "Surface", [new(0, 0, Guid.Empty)]), new(second.SurfaceId, "Surface", [new(0, 1, Guid.Empty), new(1, 2, Guid.Empty)])]));
        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, first.Part, first.LayoutRevision, AssetPath.Parse("/Content/Materials/Body.omat"));
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshSurfacesAsync().ConfigureAwait(true);
        _ = model.ShowSurfaceSelector.Should().BeTrue();
        _ = model.Surfaces.Should().OnlyHaveUniqueItems(static surface => surface.DisplayName);
        model.SelectedSurface = model.Surfaces.Single(surface => surface.Target.SurfaceId == second.SurfaceId);
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new AssetChoice("Trim", fixture.MaterialAsset, "Material", fixture.MaterialAsset.VirtualPath, IsEnabled: true, Glyph: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, second.Part, out var trim).Should().BeTrue();
        _ = trim.Should().Be(fixture.MaterialAsset);
        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, first.Part, out var body).Should().BeTrue();
        _ = body.Should().Be(AssetPath.Parse("/Content/Materials/Body.omat"));
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new AssetChoice("Clear override", Asset: null, "Geometry default", "Geometry default", IsEnabled: true, Glyph: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.Overrides.All.Should().ContainSingle().Which.Part.Should().Be(first.Part);
        _ = model.SelectedMaterialName.Should().Be("Geometry default");
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
        var target = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Authoring.Surfaces.Publish(new GeometrySurfaceMetadata(fixture.Geometry.Geometry, Guid.Parse("20000000-0000-0000-0000-000000000001"), target.LayoutRevision, [new(target.SurfaceId, "Surface", [new(0, 0, Guid.Empty)])]));
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshSurfacesAsync().ConfigureAwait(true);

        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);

        _ = model.MaterialChoices.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Contain(paint);
    });

    /// <summary>Unavailable metadata keeps the material control disabled instead of manufacturing a surface.</summary>
    /// <returns>The asynchronous unavailable-state regression.</returns>
    [TestMethod]
    public Task MaterialPickerShowsUnavailableInventory() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        fixture.Authoring.Surfaces.Remove(fixture.Geometry.Geometry);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshSurfacesAsync().ConfigureAwait(true);
        _ = model.Surfaces.Should().BeEmpty();
        _ = model.CanEditSurfaceMaterial.Should().BeFalse();
        _ = model.SurfaceNotice.Should().Contain("unavailable");
    });
}
