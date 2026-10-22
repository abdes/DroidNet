// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Authoring;

/// <summary>
/// Gestures (scene-edits.md 3.6): a property edit that previews many times and records once. Unless stated, the
/// gesture is on <c>A</c>'s <c>Transform.PositionX</c>, and <c>A</c> starts at x = 1.
/// </summary>
[TestClass]
public sealed class SceneGestureTests
{
    /// <summary>The moment at which <see cref="SceneAuthoring.HasEditsInProgress"/> is read.</summary>
    public enum Stage
    {
        /// <summary>The gesture has begun and nothing was previewed.</summary>
        AfterBegin,

        /// <summary>The gesture previewed a value other than the start.</summary>
        AfterPreview,

        /// <summary>The gesture previewed a value, then the start value again.</summary>
        AfterPreviewBackToStart,

        /// <summary>The gesture previewed a value and committed.</summary>
        AfterCommit,
    }

    /// <summary>A preview aimed at something the gesture does not edit.</summary>
    public enum Mismatch
    {
        /// <summary>Another node.</summary>
        OtherNode,

        /// <summary>Another property of the gesture's node.</summary>
        OtherProperty,
    }

    /// <summary>A call made after the gesture ended.</summary>
    public enum LateCall
    {
        /// <summary>A preview after a commit.</summary>
        PreviewAfterCommit,

        /// <summary>A second commit.</summary>
        CommitAfterCommit,

        /// <summary>A preview after a cancel.</summary>
        PreviewAfterCancel,
    }

    private static PropertyId PositionX => SceneSchema.Transform.PositionX.Id;

    [TestMethod]
    public async Task Begin_AppliesAndRaisesNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);

        _ = gesture.IsEnded.Should().BeFalse();
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(1f);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Begin_LockedTarget_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A);

        var begin = async () => _ = await BeginOnAAsync(fixture).ConfigureAwait(false);

        var refused = (await begin.Should().ThrowAsync<SceneEditRefusedException>().ConfigureAwait(false)).Which;
        SceneAssert.Refused(refused.Result!, SceneEditCodes.TargetLocked, SampleScene.A);
    }

    [TestMethod]
    public async Task Begin_MissingTarget_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var missing = NodeId.New();

        var begin = () => fixture.Authoring.BeginGestureAsync([missing], [PositionX], "Move");

        var refused = (await begin.Should().ThrowAsync<SceneEditRefusedException>().ConfigureAwait(false)).Which;
        SceneAssert.Refused(refused.Result!, SceneEditCodes.TargetMissing, missing);
    }

    [TestMethod]
    public async Task Preview_WritesTheValueAndRaisesPreview()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);

        var result = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(5f);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(SampleScene.A, [PositionX], EditPhase.Preview));
        _ = fixture.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow(Mismatch.OtherNode, DisplayName = "OtherNode")]
    [DataRow(Mismatch.OtherProperty, DisplayName = "OtherProperty")]
    public async Task Preview_OtherTargetOrProperty_IsRefused(Mismatch mismatch)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        var values = mismatch == Mismatch.OtherNode
            ? SetX(SampleScene.B, 5f)
            : new SetProperties(
                new Dictionary<NodeId, PropertyEdit> { [SampleScene.A] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionY, 5f) },
                "Move");
        var before = fixture.Snapshot();

        var result = await gesture.PreviewAsync(values).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.GestureMismatch);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(1f);
        _ = PositionOf(fixture, SampleScene.A).Y.Should().Be(0f);
        _ = PositionOf(fixture, SampleScene.B).X.Should().Be(0f);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Preview_InvalidValue_KeepsTheLastValidPreview()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);

        var result = await gesture.PreviewAsync(SetX(SampleScene.A, float.NaN)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, SampleScene.A, PositionX);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(5f);
    }

    [TestMethod]
    public async Task Commit_AfterHundredPreviews_RecordsOneEntryFromStartToLast()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        for (var x = 1; x <= 100; x++)
        {
            _ = await gesture.PreviewAsync(SetX(SampleScene.A, x)).ConfigureAwait(false);
        }

        var result = await gesture.CommitAsync().ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var afterUndo = PositionOf(fixture, SampleScene.A).X;
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = recorded.Should().Be(1);
        _ = afterUndo.Should().Be(1f);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(100f);
    }

    [TestMethod]
    public async Task Commit_RaisesCommit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        fixture.Changes.Clear();

        _ = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(SampleScene.A, [PositionX], EditPhase.Commit));
    }

    [TestMethod]
    public async Task Commit_BackAtTheStartValue_RecordsNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 1f)).ConfigureAwait(false);

        var result = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().BeEmpty();
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(1f);
    }

    [TestMethod]
    public async Task Commit_WithoutPreview_RecordsNothing()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);

        var result = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Cancel_RestoresTheStartValueAndRaisesCancel()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        fixture.Changes.Clear();

        await gesture.CancelAsync().ConfigureAwait(false);

        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(1f);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(SampleScene.A, [PositionX], EditPhase.Cancel));
        _ = fixture.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow(LateCall.PreviewAfterCommit, DisplayName = "PreviewAfterCommit")]
    [DataRow(LateCall.CommitAfterCommit, DisplayName = "CommitAfterCommit")]
    [DataRow(LateCall.PreviewAfterCancel, DisplayName = "PreviewAfterCancel")]
    public async Task Call_AfterTheGestureEnded_IsRefused(LateCall call)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        if (call == LateCall.PreviewAfterCancel)
        {
            await gesture.CancelAsync().ConfigureAwait(false);
        }
        else
        {
            _ = await gesture.CommitAsync().ConfigureAwait(false);
        }

        var x = PositionOf(fixture, SampleScene.A).X;
        var before = fixture.Snapshot();

        var result = await (call == LateCall.CommitAfterCommit ? gesture.CommitAsync() : gesture.PreviewAsync(SetX(SampleScene.A, 9f))).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.GestureMismatch);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(x);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Preview_AfterTargetRemoved_CancelsTheGesture()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RemoveNodes([SampleScene.A])).ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;

        var result = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.GestureCancelled);
        _ = fixture.History.UndoStack.Should().HaveCount(recorded);
        _ = fixture.Scene.AllNodes.Select(static node => node.Id).Should().NotContain(SampleScene.A);
    }

    [TestMethod]
    public async Task Commit_AfterComponentRemovedAndRestored_CancelsTheGesture()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await fixture.Authoring.BeginGestureAsync([SampleScene.Lamp], [SceneProperties.PointLight.RangeDescriptor.Id], "Range").ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RemoveComponent(SampleScene.Lamp, SampleScene.LampLight)).ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;

        var result = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.GestureCancelled);
        _ = fixture.History.UndoStack.Should().HaveCount(recorded);
    }

    [TestMethod]
    public async Task Commit_RotationAcrossGimbalLock_UndoRestoresTheOrientation()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var orient = new PropertyEdit.Builder()
            .Set(SceneSchema.Transform.RotationX, 90f)
            .Set(SceneSchema.Transform.RotationY, 25f)
            .Set(SceneSchema.Transform.RotationZ, 0f)
            .Build();
        _ = await fixture.Authoring.ExecuteAsync(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.A1] = orient }, "Orient")).ConfigureAwait(false);
        var start = RotationOf(fixture, SampleScene.A1);
        var gesture = await fixture.Authoring.BeginGestureAsync([SampleScene.A1], [SceneSchema.Transform.RotationX.Id], "Rotate").ConfigureAwait(false);
        _ = await gesture.PreviewAsync(new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.A1] = PropertyEdit.SingleEdit(SceneSchema.Transform.RotationX, 15f) }, "Rotate")).ConfigureAwait(false);
        _ = await gesture.CommitAsync().ConfigureAwait(false);

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.SameRotation(RotationOf(fixture, SampleScene.A1), start);
    }

    [TestMethod]
    [DataRow(Stage.AfterBegin, false, DisplayName = "AfterBegin")]
    [DataRow(Stage.AfterPreview, true, DisplayName = "AfterPreview")]
    [DataRow(Stage.AfterPreviewBackToStart, false, DisplayName = "AfterPreviewBackToStart")]
    [DataRow(Stage.AfterCommit, false, DisplayName = "AfterCommit")]
    public async Task HasEditsInProgress(Stage stage, bool expected)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        if (stage != Stage.AfterBegin)
        {
            _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        }

        if (stage == Stage.AfterPreviewBackToStart)
        {
            _ = await gesture.PreviewAsync(SetX(SampleScene.A, 1f)).ConfigureAwait(false);
        }

        if (stage == Stage.AfterCommit)
        {
            _ = await gesture.CommitAsync().ConfigureAwait(false);
        }

        var inProgress = fixture.Authoring.HasEditsInProgress;

        _ = inProgress.Should().Be(expected);
    }

    [TestMethod]
    public async Task FinishEdits_CommitsEveryOpenGesture()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var onA = await BeginOnAAsync(fixture).ConfigureAwait(false);
        var onB = await fixture.Authoring.BeginGestureAsync([SampleScene.B], [PositionX], "Move").ConfigureAwait(false);
        _ = await onA.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        _ = await onB.PreviewAsync(SetX(SampleScene.B, 6f)).ConfigureAwait(false);

        await fixture.Authoring.FinishEditsAsync().ConfigureAwait(false);

        _ = fixture.History.UndoStack.Should().HaveCount(2);
        _ = fixture.Authoring.HasEditsInProgress.Should().BeFalse();
    }

    [TestMethod]
    public async Task Dispose_CancelsOpenGestures()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);

        fixture.Authoring.Dispose();

        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(1f);
        _ = fixture.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task SceneProperties_PreviewAndCommit_RecordOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var exposure = SceneProperties.SceneEnvironment.ExposureCompensation;
        var gesture = await fixture.Authoring.BeginGestureAsync([], [exposure.Id], "Exposure").ConfigureAwait(false);

        var preview = await gesture.PreviewAsync(new SetSceneProperties(PropertyEdit.SingleEdit(exposure, 1.5f), "Exposure")).ConfigureAwait(false);
        var previewed = fixture.Changes.Only();
        var commit = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Applied(preview);
        SceneAssert.Changes(previewed, new SceneChange.PropertiesChanged(Node: null, [exposure.Id], EditPhase.Preview));
        SceneAssert.Applied(commit);
        _ = fixture.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task EditOnAnotherNodeDuringGesture_LeavesTheGestureOpen()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 6f)).ConfigureAwait(false);

        var result = await gesture.CommitAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().HaveCount(2);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(6f);
    }

    [TestMethod]
    public async Task OneShotEditOfTheSameNodeDuringGesture_AppliesAndTheGestureGoesOn()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginOnAAsync(fixture).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(SetX(SampleScene.A, 5f)).ConfigureAwait(false);
        var oneShot = new SetProperties(
            new Dictionary<NodeId, PropertyEdit> { [SampleScene.A] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionY, 3f) },
            "Lift");
        var applied = await fixture.Authoring.ExecuteAsync(oneShot).ConfigureAwait(false);

        var result = await gesture.PreviewAsync(SetX(SampleScene.A, 6f)).ConfigureAwait(false);

        SceneAssert.Applied(applied);
        SceneAssert.Applied(result);
        _ = PositionOf(fixture, SampleScene.A).X.Should().Be(6f);
        _ = PositionOf(fixture, SampleScene.A).Y.Should().Be(3f);
    }

    [TestMethod]
    public async Task Preview_AfterUndoRemovedTheTargetComponent_CancelsTheGesture()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var light = new PointLightData { Id = ComponentId.New(), Name = "Light" };
        _ = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.B, light, index: 1)).ConfigureAwait(false);
        var range = SceneProperties.PointLight.RangeDescriptor;
        var gesture = await fixture.Authoring.BeginGestureAsync([SampleScene.B], [range.Id], "Range").ConfigureAwait(false);
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var recorded = fixture.History.UndoStack.Count;
        var values = new SetProperties(
            new Dictionary<NodeId, PropertyEdit> { [SampleScene.B] = PropertyEdit.SingleEdit(range.TypedId, 20f) },
            "Range");

        var result = await gesture.PreviewAsync(values).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.GestureCancelled);
        _ = fixture.History.UndoStack.Should().HaveCount(recorded);
    }

    private static Task<SceneGesture> BeginOnAAsync(AuthoringFixture fixture)
        => fixture.Authoring.BeginGestureAsync([SampleScene.A], [PositionX], "Move");

    private static SetProperties SetX(NodeId node, float x)
        => new(new Dictionary<NodeId, PropertyEdit> { [node] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, x) }, "Move");

    private static Vector3 PositionOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<TransformComponent>().Single().LocalPosition;

    private static Quaternion RotationOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<TransformComponent>().Single().LocalRotation;
}
