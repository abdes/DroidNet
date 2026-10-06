// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.TestSupport;

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
        using var fixture = new DemandFixture();
        fixture.Activate();
        var uri = fixture.Geometry.Geometry!.Uri;
        var first = SceneAuthoringFixture.TargetFor(uri);
        var second = first with
        {
            SlotId = Guid.Parse("10000000-0000-0000-0000-000000000002"),
        };
        _ = fixture.Authoring.Slots.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), uri, It.IsAny<CancellationToken>())).ReturnsAsync(new GeometryMaterialSlotMetadata(uri, Guid.Parse("20000000-0000-0000-0000-000000000001"), first.LayoutRevision, [new(first.SlotId, "Surface", [new(0, 0, Guid.Empty)]), new(second.SlotId, "Surface", [new(0, 1, Guid.Empty), new(1, 2, Guid.Empty)])]));
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Target = first, Material = new(new Uri("asset:///Content/Materials/Body.omat.json")) });
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        _ = model.ShowMaterialSlotSelector.Should().BeTrue();
        _ = model.MaterialSlots.Should().OnlyHaveUniqueItems(static slot => slot.DisplayName);
        model.SelectedMaterialSlot = model.MaterialSlots.Single(slot => slot.Target.SlotId == second.SlotId);
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new("Trim", fixture.MaterialUri, "Material", fixture.MaterialUri.AbsolutePath, AssetPickerGroup.Content, IsEnabled: true, ThumbnailModel: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single(slot => slot.Target.SlotId == second.SlotId).Material.Uri.Should().Be(fixture.MaterialUri);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single(slot => slot.Target.SlotId == first.SlotId).Material.Uri.Should().Be(new Uri("asset:///Content/Materials/Body.omat.json"));
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new("Clear override", Uri: null, "Geometry default", "Geometry default", AssetPickerGroup.Engine, IsEnabled: true, ThumbnailModel: string.Empty)).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle().Which.Target.SlotId.Should().Be(first.SlotId);
        _ = model.SelectedMaterialName.Should().Be("Geometry default");
    });

    /// <summary>Unavailable metadata keeps the material control disabled instead of manufacturing a slot.</summary>
    /// <returns>The asynchronous unavailable-state regression.</returns>
    [TestMethod]
    public Task MaterialPickerShowsUnavailableInventory() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        _ = fixture.Authoring.Slots.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<Uri>(), It.IsAny<CancellationToken>())).ReturnsAsync((GeometryMaterialSlotMetadata?)null);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        _ = model.MaterialSlots.Should().BeEmpty();
        _ = model.CanEditMaterialSlot.Should().BeFalse();
        _ = model.MaterialSlotNotice.Should().Contain("unavailable");
    });
}
