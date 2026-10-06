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
        _ = context.Metadata.IsDirty.Should().BeFalse();
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
        _ = context.Metadata.IsDirty.Should().BeTrue();
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
        _ = context.Metadata.IsDirty.Should().BeFalse();
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
        _ = context.Metadata.IsDirty.Should().BeFalse();
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
        _ = context.Metadata.IsDirty.Should().BeFalse();
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
