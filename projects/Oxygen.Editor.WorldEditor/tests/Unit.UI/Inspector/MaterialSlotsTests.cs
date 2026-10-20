// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class MaterialSlotsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The slot selector applies a nonzero slot without touching another slot's override.</summary>
    /// <returns>The asynchronous Inspector regression.</returns>
    [TestMethod]
    public Task MaterialPickerEditsSelectedNativeSlotAndClearRemovesIt() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        var geometry = fixture.Geometry.Geometry;
        var first = SceneAuthoringFixture.TargetFor(geometry);
        var second = first with
        {
            SlotId = Guid.Parse("10000000-0000-0000-0000-000000000002"),
        };
        fixture.Authoring.Slots.Publish(new GeometryMaterialSlotMetadata(geometry, Guid.Parse("20000000-0000-0000-0000-000000000001"), first.LayoutRevision, [new(first.SlotId, "Surface", [new(0, 0, Guid.Empty)]), new(second.SlotId, "Surface", [new(0, 1, Guid.Empty), new(1, 2, Guid.Empty)])]));
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Target = first, Material = AssetPath.Parse("/Content/Materials/Body.omat") });
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        _ = model.ShowMaterialSlotSelector.Should().BeTrue();
        _ = model.MaterialSlots.Should().OnlyHaveUniqueItems(static slot => slot.DisplayName);
        model.SelectedMaterialSlot = model.MaterialSlots.Single(slot => slot.Target.SlotId == second.SlotId);
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new AssetChoice("Trim", fixture.MaterialAsset, "Material", fixture.MaterialAsset.VirtualPath, IsEnabled: true, Glyph: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single(slot => slot.Target.SlotId == second.SlotId).Material.Should().Be(fixture.MaterialAsset);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single(slot => slot.Target.SlotId == first.SlotId).Material.Should().Be(AssetPath.Parse("/Content/Materials/Body.omat"));
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new AssetChoice("Clear override", Asset: null, "Geometry default", "Geometry default", IsEnabled: true, Glyph: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle().Which.Target.SlotId.Should().Be(first.SlotId);
        _ = model.SelectedMaterialName.Should().Be("Geometry default");
    });

    /// <summary>
    /// A model's material is not listed in the Content Browser, and is still offered by the material picker: it is
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
        fixture.Authoring.Slots.Publish(new GeometryMaterialSlotMetadata(fixture.Geometry.Geometry, Guid.Parse("20000000-0000-0000-0000-000000000001"), target.LayoutRevision, [new(target.SlotId, "Surface", [new(0, 0, Guid.Empty)])]));
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);

        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);

        _ = model.MaterialChoices.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Contain(paint);
    });

    /// <summary>Unavailable metadata keeps the material control disabled instead of manufacturing a slot.</summary>
    /// <returns>The asynchronous unavailable-state regression.</returns>
    [TestMethod]
    public Task MaterialPickerShowsUnavailableInventory() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        fixture.Authoring.Slots.Remove(fixture.Geometry.Geometry);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        _ = model.MaterialSlots.Should().BeEmpty();
        _ = model.CanEditMaterialSlot.Should().BeFalse();
        _ = model.MaterialSlotNotice.Should().Contain("unavailable");
    });
}
