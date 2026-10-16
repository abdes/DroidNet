// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    [DataRow("?variant=other")]
    [DataRow("#other")]
    public async Task MaterialAssignmentRejectsUnsupportedUriComponentsBeforeMutation(string suffix)
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = AddSlotGeometry(scene, "Cube", "Cube");
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        var context = CreateContext(scene);
        var result = await fixture.Sut.EditMaterialSlotAsync(context, [node.Id], TestSlotTarget(geometry.Geometry!.Uri),
            new Uri("asset:///Content/Materials/Assigned.omat.json" + suffix), EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = geometry.OverrideSlots.Should().BeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.IsDirty().Should().BeFalse();
        fixture.Inventories.Verify(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<Uri>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    [TestMethod]
    public async Task MaterialAssignmentTargetsNonzeroIdentityAcrossMatchingInstances()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = AddSlotGeometry(scene, "First", "Cube");
        var second = AddSlotGeometry(scene, "Second", "Cube");
        var geometry = first.Components.OfType<GeometryComponent>().Single();
        var target = TestSlotTarget(geometry.Geometry!.Uri) with { SlotId = SecondarySlotId };
        var material = new Uri("asset:///Content/Materials/Trim.omat.json");
        _ = fixture.Sync.Setup(value => value.UpdateMaterialSlotAsync(scene, It.IsAny<SceneNode>(), target, material, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditMaterialSlot, AffectedScope.Empty));
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditMaterialSlotAsync(context, [first.Id, second.Id], target, material, EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        foreach (var node in new[] { first, second })
        {
            var stored = node.Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle().Which;
            _ = stored.Target.Should().Be(target);
            _ = stored.Material.Uri.Should().Be(material);
        }

        _ = context.History.UndoStack.Should().ContainSingle();
        _ = context.IsDirty().Should().BeTrue();
        fixture.Sync.Verify(value => value.UpdateMaterialSlotAsync(scene, It.IsAny<SceneNode>(), target, material, It.IsAny<CancellationToken>()), Times.Exactly(2));
    }

    [TestMethod]
    public async Task MaterialAssignmentRejectsDifferentGeometryWithoutPartialMutation()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = AddSlotGeometry(scene, "Cube", "Cube");
        var second = AddSlotGeometry(scene, "Sphere", "Sphere");
        var context = CreateContext(scene);
        var target = TestSlotTarget(first.Components.OfType<GeometryComponent>().Single().Geometry!.Uri);

        var result = await fixture.Sut.EditMaterialSlotAsync(context, [first.Id, second.Id], target,
            new Uri("asset:///Content/Materials/Assigned.omat.json"), EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = first.Components.OfType<GeometryComponent>().Single().OverrideSlots.Should().BeEmpty();
        _ = second.Components.OfType<GeometryComponent>().Single().OverrideSlots.Should().BeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.IsDirty().Should().BeFalse();
        fixture.Inventories.Verify(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<Uri>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    [TestMethod]
    public async Task MaterialAssignmentRejectsChangedInventoryRevision()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = AddSlotGeometry(scene, "Cube", "Cube");
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        var target = TestSlotTarget(geometry.Geometry!.Uri);
        _ = fixture.Inventories.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), target.GeometryUri, It.IsAny<CancellationToken>()))
            .ReturnsAsync(TestSlotInventory(target.GeometryUri) with { LayoutRevision = new string('b', 64) });
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditMaterialSlotAsync(context, [node.Id], target,
            new Uri("asset:///Content/Materials/Assigned.omat.json"), EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = geometry.OverrideSlots.Should().BeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.IsDirty().Should().BeFalse();
    }

    [TestMethod]
    [DataRow("geometry")]
    [DataRow("project")]
    [DataRow("node")]
    public async Task MaterialAssignmentRevalidatesAfterInventoryAwait(string changed)
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = AddSlotGeometry(scene, "Cube", "Cube");
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        var target = TestSlotTarget(geometry.Geometry!.Uri);
        var inventory = new TaskCompletionSource<GeometryMaterialSlotMetadata?>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Inventories.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), target.GeometryUri, It.IsAny<CancellationToken>())).Returns(inventory.Task);
        var context = CreateContext(scene);
        var pending = fixture.Sut.EditMaterialSlotAsync(context, [node.Id], target, new Uri("asset:///Content/Materials/Assigned.omat.json"), EditSessionToken.OneShot);
        switch (changed)
        {
            case "geometry": geometry.Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Sphere")); break;
            case "project": fixture.Projects.Close(); break;
            case "node": _ = scene.RootNodes.Remove(node); break;
        }

        inventory.SetResult(TestSlotInventory(target.GeometryUri));
        _ = (await pending.ConfigureAwait(false)).Succeeded.Should().BeFalse();
        _ = geometry.OverrideSlots.Should().BeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.IsDirty().Should().BeFalse();
    }

    [TestMethod]
    public async Task ClearUnresolvedRemovesOnlyOverridesTheCurrentGeometryCannotResolve()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = AddSlotGeometry(scene, "Replaced", "Sphere");
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        var material = new AssetReference<MaterialAsset>(new Uri("asset:///Content/Materials/Brick.omat.json"));
        var foreign = new MaterialsSlot { Target = TestSlotTarget(AssetUris.BuildGeneratedUri("BasicShapes/Cube")), Material = material };
        var resolved = new MaterialsSlot { Target = TestSlotTarget(geometry.Geometry!.Uri), Material = material };
        var missing = new MaterialsSlot { Target = TestSlotTarget(geometry.Geometry.Uri) with { SlotId = Guid.NewGuid() }, Material = material };
        geometry.OverrideSlots.Add(foreign);
        geometry.OverrideSlots.Add(resolved);
        geometry.OverrideSlots.Add(missing);
        var context = CreateContext(scene);

        var result = await fixture.Sut.ClearUnresolvedMaterialSlotsAsync(context, [node.Id], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = geometry.OverrideSlots.Should().Equal(resolved);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = context.IsDirty().Should().BeTrue();
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = geometry.OverrideSlots.Should().BeEquivalentTo([foreign, resolved, missing]);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = geometry.OverrideSlots.Should().Equal(resolved);
        fixture.Sync.Verify(
            value => value.UpdateMaterialSlotAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<MaterialSlotTarget>(), It.IsAny<Uri?>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    private static SceneNode AddSlotGeometry(Scene scene, string name, string primitive)
    {
        var node = new SceneNode(scene) { Name = name };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/" + primitive)),
        });
        scene.RootNodes.Add(node);
        return node;
    }
}
