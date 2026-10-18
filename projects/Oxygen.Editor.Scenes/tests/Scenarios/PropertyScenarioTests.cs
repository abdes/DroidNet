// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Scenarios;

/// <summary>
/// What a user does to properties from the inspector and the gizmos: batches, one-shot edits and gestures, and how they
/// meet saves and undos made elsewhere.
/// </summary>
[TestClass]
public sealed class PropertyScenarioTests
{
    private const int Previews = 100;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ResetAtmosphereLights_AsOneBatch_UndoRestoresBoth()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        // The sun moves from Sun to Moon: the old owner is cleared first, then the new one takes the slot.
        var moved = await fixture.Authoring.ExecuteAsync(new SceneEditBatch(
            "Make Moon the sun",
            [SlotOf(SampleScene.Sun, AtmosphereLightSlot.None), SlotOf(SampleScene.Moon, AtmosphereLightSlot.Primary)])).ConfigureAwait(false);
        var swapped = (SlotOf(fixture, SampleScene.Sun), SlotOf(fixture, SampleScene.Moon));
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(moved);
        _ = swapped.Should().Be((AtmosphereLightSlot.None, AtmosphereLightSlot.Primary));
        SceneAssert.Applied(undone);
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "one undo restores both lights");
    }

    [TestMethod]
    public async Task MoveCamera_PositionAndRotation_IsOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var move = new PropertyEdit.Builder()
            .Set(SceneProperties.Transform.PositionX, 3f)
            .Set(SceneProperties.Transform.PositionY, 2f)
            .Set(SceneProperties.Transform.PositionZ, -5f)
            .Set(SceneProperties.Transform.RotationY, 30f)
            .Build();

        var result = await fixture.Authoring.ExecuteAsync(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Cam] = move }, "Move camera")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().HaveCount(before.UndoCount + 1);
        _ = TransformOf(fixture, SampleScene.Cam).LocalPosition.Should().Be(new Vector3(3f, 2f, -5f));
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "one undo restores the position and the rotation");
    }

    [TestMethod]
    public async Task GizmoDrag_HundredPreviews_UndoRedo()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var gesture = await fixture.Authoring.BeginGestureAsync([SampleScene.Cam], [SceneProperties.Transform.PositionX.Id], "Drag").ConfigureAwait(false);
        for (var step = 1; step <= Previews; step++)
        {
            SceneAssert.Applied(await gesture.PreviewAsync(MoveX(SampleScene.Cam, step / 10f)).ConfigureAwait(false));
        }

        var committed = await gesture.CommitAsync().ConfigureAwait(false);
        var dragged = fixture.Snapshot();

        SceneAssert.Applied(committed);
        _ = dragged.UndoCount.Should().Be(before.UndoCount + 1, "a drag is one entry, however many previews");
        _ = dragged.ChangedCount.Should().Be(before.ChangedCount + Previews + 1, "every preview and the commit are heard");
        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "undo returns to where the drag began");
        SceneAssert.Applied(await fixture.Authoring.RedoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(dragged.Document, "redo returns to where the drag ended");
    }

    [TestMethod]
    public async Task InspectorEditThenSave_FinishesTheGestureFirst()
    {
        using var documents = new SceneDocuments();
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var authoring = SceneContent.Of(document).Authoring;
        var gesture = await authoring.BeginGestureAsync([SampleScene.A], [SceneProperties.Transform.PositionX.Id], "Position").ConfigureAwait(false);
        _ = await gesture.PreviewAsync(MoveX(SampleScene.A, 5f)).ConfigureAwait(false);

        var saved = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeTrue(saved.Message);
        _ = gesture.IsEnded.Should().BeTrue("the save commits the open gesture");
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = document.IsDirty.Should().BeFalse("the saved file holds the committed value");
        var stored = await documents.Files.ReadAsync(document.File.Path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var file = JsonSerializer.Deserialize(stored.Content.AsSpan(), SceneJsonContext.Default.SceneData)!;
        _ = file.RootNodes.Single(node => node.Id == SampleScene.A).Components.OfType<TransformData>().Single().Position.X.Should().Be(5f);
    }

    [TestMethod]
    public async Task UndoInOneViewDuringDragInAnother_CancelsTheDrag()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = SceneNodeTemplates.Empty(fixture.Scene);
        _ = await fixture.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(fixture.Scene, fixture.Layout, node)])).ConfigureAwait(false);
        var drag = await fixture.Authoring.BeginGestureAsync([node.Id], [SceneProperties.Transform.PositionX.Id], "Drag").ConfigureAwait(false);
        _ = await drag.PreviewAsync(MoveX(node.Id, 1f)).ConfigureAwait(false);

        // The other view undoes without finishing the drag, which removes the dragged node.
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var next = await drag.PreviewAsync(MoveX(node.Id, 2f)).ConfigureAwait(false);

        SceneAssert.Refused(next, SceneEditCodes.GestureCancelled);
        _ = drag.IsEnded.Should().BeTrue();
        _ = fixture.History.UndoStack.Should().BeEmpty("the drag recorded nothing");
        _ = fixture.History.RedoStack.Should().ContainSingle("the undone insert can still be redone");
    }

    private static SetProperties SlotOf(NodeId light, AtmosphereLightSlot slot)
        => new(new Dictionary<NodeId, PropertyEdit> { [light] = PropertyEdit.SingleEdit(SceneProperties.DirectionalLight.AtmosphereSlot, slot) }, "Atmosphere slot");

    private static SetProperties MoveX(NodeId node, float x)
        => new(new Dictionary<NodeId, PropertyEdit> { [node] = PropertyEdit.SingleEdit(SceneProperties.Transform.PositionX, x) }, "Move");

    private static AtmosphereLightSlot SlotOf(AuthoringFixture fixture, NodeId light)
        => fixture.Node(light).Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot;

    private static TransformComponent TransformOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<TransformComponent>().Single();
}
