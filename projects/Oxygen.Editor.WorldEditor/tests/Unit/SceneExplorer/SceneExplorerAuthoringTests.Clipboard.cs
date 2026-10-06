// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Controls;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

public sealed partial class SceneExplorerAuthoringTests
{
    private static readonly JsonSerializerOptions ClipboardJsonOptions = new() { IncludeFields = true };

    [TestMethod]
    public async Task CopyItemsAsync_SourceEditedAfterCapture_RepeatedPasteUsesImmutableFullSubtree()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var child = new SceneNode(scene) { Name = "Collapsed child" };
        node.AddChild(child);
        var childTransform = child.Components.OfType<TransformComponent>().Single();
        childTransform.LocalPosition = new Vector3(1f, 2f, 3f);
        var light = new PointLightComponent { Name = "Light", LuminousFluxLumens = 123f };
        _ = child.AddComponent(light);
        var pasted = new List<string>();
        _ = harness.Commands
            .Setup(value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null))
            .Callback<SceneDocumentCommandContext, IReadOnlyList<SceneNodeData>, Guid?, Guid?, Guid?>((context, data, _, _, _) =>
            {
                _ = context.Scene.Should().BeSameAs(scene);
                pasted.Add(JsonSerializer.Serialize(data, ClipboardJsonOptions));
            })
            .ReturnsAsync(SceneCommandResults.Success<IReadOnlyList<SceneNode>>([]));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();
        _ = adapter!.IsExpanded.Should().BeFalse();

        await explorer.CopyItemsAsync([adapter]).ConfigureAwait(false);
        var captured = JsonSerializer.Serialize<IReadOnlyList<SceneNodeData>>([node.Dehydrate()], ClipboardJsonOptions);
        node.Name = "Edited root";
        child.Name = "Edited child";
        childTransform.LocalPosition = new Vector3(9f);
        light.LuminousFluxLumens = 456f;
        _ = child.RemoveComponent(light);
        node.AddChild(new SceneNode(scene) { Name = "Added after Copy" });

        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);
        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);

        _ = pasted.Should().Equal(
            new[] { captured, captured },
            because: "collapsed children and component values are captured once, not reread from live nodes");
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = explorer.UndoStack.Should().BeEmpty("capture and the mocked routing boundary do not author a source transaction");
        _ = harness.Documents.Object.GetOpenDocuments(default).Single().IsDirty.Should().BeFalse();
        harness.Commands.Verify(
            value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null),
            Times.Exactly(2));
    }

    [TestMethod]
    public async Task CopyItemsAsync_SameProjectSequentialScene_PastesCapturedLocalPoseIntoDestinationOnly()
    {
        var harness = new AuthoringHarness(out var sourceScene, out var source);
        var destination = harness.AddScene("Destination");
        var sourceTransform = ArrangeParentedClipboardSource(sourceScene, source);
        var capturedWorld = SceneTransformMath.LocalMatrix(sourceTransform);
        SceneDocumentCommandContext? pasteContext = null;
        SceneNodeData? pasted = null;
        _ = harness.Commands
            .Setup(value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null))
            .Callback<SceneDocumentCommandContext, IReadOnlyList<SceneNodeData>, Guid?, Guid?, Guid?>((context, data, _, _, _) =>
            {
                pasteContext = context;
                pasted = data.Single();
            })
            .ReturnsAsync(SceneCommandResults.Success<IReadOnlyList<SceneNode>>([]));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(sourceScene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(source.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();
        await explorer.CopyItemsAsync([adapter!]).ConfigureAwait(false);
        var sourceHistory = explorer.UndoStack;
        source.Name = "Edited after Copy";
        sourceTransform.LocalPosition = Vector3.Zero;

        await explorer.HandleDocumentOpenedAsync(destination).ConfigureAwait(false);
        _ = explorer.Scene!.AttachedObject.Should().BeSameAs(destination);
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);

        _ = pasteContext.Should().NotBeNull();
        _ = pasteContext!.Scene.Should().BeSameAs(destination);
        _ = pasteContext.DocumentId.Should().Be(destination.Id);
        _ = pasted.Should().NotBeNull();
        _ = pasted!.Name.Should().Be("Node");
        AssertClipboardWorldPose(destination, pasted, capturedWorld);
        _ = sourceHistory.Should().BeEmpty();
        _ = explorer.UndoStack.Should().BeEmpty("the mock proves routing; destination authoring history is verified by the command tests");
        _ = harness.Documents.Object.GetOpenDocuments(default).Should().OnlyContain(metadata => !metadata.IsDirty);
        harness.Commands.Verify(
            value => value.DuplicateNodesFromDataAsync(
                It.Is<SceneDocumentCommandContext>(context => context.Scene == sourceScene),
                It.IsAny<IReadOnlyList<SceneNodeData>>(),
                null,
                null,
                null),
            Times.Never);
    }

    [TestMethod]
    public async Task CopyItemsAsync_UnrepresentableWorldPose_CapturesEntirePreserveLocalBatch()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        var parent = new SceneNode(scene) { Name = "Scaled parent" };
        parent.Components.OfType<TransformComponent>().Single().LocalScale = new Vector3(2f, 1f, 1f);
        var child = new SceneNode(scene) { Name = "Rotated child" };
        child.Components.OfType<TransformComponent>().Single().LocalRotation = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 4f);
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        IReadOnlyList<SceneNodeData>? pasted = null;
        _ = harness.Commands
            .Setup(value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null))
            .Callback<SceneDocumentCommandContext, IReadOnlyList<SceneNodeData>, Guid?, Guid?, Guid?>((_, data, _, _, _) => pasted = data)
            .ReturnsAsync(SceneCommandResults.Success<IReadOnlyList<SceneNode>>([]));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        var childAdapter = await explorer.FindAdapterByNodeIdAsync(child.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();
        _ = childAdapter.Should().NotBeNull();
        await explorer.CopyItemsAsync([adapter!]).ConfigureAwait(false);
        node.Name = "Edited after Copy";

        await explorer.CopyItemsAsync([adapter!, childAdapter!]).ConfigureAwait(false);
        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);

        _ = pasted.Should().NotBeNull();
        _ = pasted!.Select(data => data.Id).Should().Equal(node.Id, child.Id);
        _ = pasted[0].Name.Should().Be("Edited after Copy");
        _ = pasted[1].Name.Should().Be("Rotated child");
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = explorer.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task CutItemsAsync_EscapeClearClipboardIntent_CancelsMarksWithoutAuthoring()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();
        var before = JsonSerializer.Serialize(scene.Dehydrate(), ClipboardJsonOptions);

        await explorer.CutItemsAsync([adapter!]).ConfigureAwait(false);

        _ = adapter!.IsCut.Should().BeTrue();
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Cut);
        _ = explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        _ = explorer.UndoStack.Should().BeEmpty();

        // DynamicTree's Escape handler dispatches this intent; no workstation keyboard injection is needed.
        await explorer.ClearClipboardAsync().ConfigureAwait(false);
        await explorer.PasteItemsAsync(explorer.Scene).ConfigureAwait(false);

        _ = adapter.IsCut.Should().BeFalse();
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = explorer.PasteCommand.CanExecute(null).Should().BeFalse();
        _ = JsonSerializer.Serialize(scene.Dehydrate(), ClipboardJsonOptions).Should().Be(before);
        _ = explorer.UndoStack.Should().BeEmpty();
        _ = explorer.RedoStack.Should().BeEmpty();
        _ = harness.Documents.Object.GetOpenDocuments(default).Single().IsDirty.Should().BeFalse();
        harness.Commands.VerifyNoOtherCalls();
    }

    private static TransformComponent ArrangeParentedClipboardSource(Scene scene, SceneNode source)
    {
        var parent = new SceneNode(scene) { Name = "Source parent" };
        var parentTransform = parent.Components.OfType<TransformComponent>().Single();
        parentTransform.LocalPosition = new Vector3(10f, 20f, 30f);
        parentTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.2f, 0.3f, 0.4f);
        parentTransform.LocalScale = new Vector3(2f);
        scene.RootNodes.Clear();
        scene.RootNodes.Add(parent);
        parent.AddChild(source);
        var sourceTransform = source.Components.OfType<TransformComponent>().Single();
        sourceTransform.LocalPosition = new Vector3(1f, 2f, 3f);
        sourceTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.1f, 0.4f, 0.2f);
        sourceTransform.LocalScale = new Vector3(1f, 2f, 3f);
        return sourceTransform;
    }

    private static void AssertClipboardWorldPose(Scene destination, SceneNodeData pasted, Matrix4x4 capturedWorld)
    {
        var pastedNode = SceneNode.CreateAndHydrate(destination, pasted);
        var pastedWorld = SceneTransformMath.WorldMatrix(pastedNode);
        _ = (pastedWorld.Translation - capturedWorld.Translation).Length().Should().BeLessThan(1e-3f);
        _ = Matrix4x4.Decompose(pastedWorld, out var pastedScale, out var pastedRotation, out _).Should().BeTrue();
        _ = Matrix4x4.Decompose(capturedWorld, out var capturedScale, out var capturedRotation, out _).Should().BeTrue();
        _ = Vector3.Distance(pastedScale, capturedScale).Should().BeLessThan(1e-3f);
        _ = MathF.Abs(Quaternion.Dot(pastedRotation, capturedRotation)).Should().BeApproximately(1f, 1e-3f);
    }
}
