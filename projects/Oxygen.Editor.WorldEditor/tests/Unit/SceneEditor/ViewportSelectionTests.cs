// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

/// <summary>
/// Viewport selection: how picks combine with the selection, which picked nodes a click or a
/// marquee selects, and framing the selection.
/// </summary>
[TestClass]
[TestCategory("Viewport Selection")]
public sealed class ViewportSelectionTests
{
    private static readonly RuntimeViewId ViewId = new(83);
    private static readonly Guid A = Guid.NewGuid();
    private static readonly Guid B = Guid.NewGuid();
    private static readonly Guid C = Guid.NewGuid();

    [TestMethod]
    public void Combine_Replace_ShouldSelectThePickedNodes()
        => _ = new ViewportSelection([B, C], ViewportSelectionMode.Replace).Combine([A]).Should().Equal(B, C);

    [TestMethod]
    public void Combine_ReplaceWithNothingPicked_ShouldClearTheSelection()
        => _ = new ViewportSelection([], ViewportSelectionMode.Replace).Combine([A, B]).Should().BeEmpty();

    [TestMethod]
    public void Combine_ReplaceWithNothingPickedAndNothingSelected_ShouldChangeNothing()
        => _ = new ViewportSelection([], ViewportSelectionMode.Replace).Combine([]).Should().BeNull();

    [TestMethod]
    public void Combine_AddOrToggleWithNothingPicked_ShouldChangeNothing()
    {
        _ = new ViewportSelection([], ViewportSelectionMode.Add).Combine([A]).Should().BeNull();
        _ = new ViewportSelection([], ViewportSelectionMode.Toggle).Combine([A]).Should().BeNull();
    }

    [TestMethod]
    public void Combine_Add_ShouldAppendThePickedNodesAndMakeTheLastActive()
        => _ = new ViewportSelection([A, C], ViewportSelectionMode.Add).Combine([A, B]).Should().Equal(B, A, C);

    [TestMethod]
    public void Combine_Toggle_ShouldRemoveSelectedNodesAndAddTheOthers()
        => _ = new ViewportSelection([A, C, C], ViewportSelectionMode.Toggle).Combine([A, B]).Should().Equal(B, C);

    [TestMethod]
    public async Task PickAsync_Click_ShouldSelectTheHitNearestThePointer()
    {
        var engine = PickingEngine(new RuntimePickResult([Hit(B, 0.5f), Hit(A, 2.0f), Hit(B, 3.0f)], WorldPosition: null));
        using var sut = CreateViewport(engine.Object);
        ViewportSelection? picked = null;
        sut.SelectionPicked = selection => picked = selection;

        await sut.PickAsync(new RuntimePickRect(10, 10, 7, 7), ViewportSelectionMode.Toggle, isMarquee: false).ConfigureAwait(false);

        _ = picked.Should().NotBeNull();
        _ = picked!.NodeIds.Should().Equal(B);
        _ = picked.Mode.Should().Be(ViewportSelectionMode.Toggle);
    }

    [TestMethod]
    public async Task PickAsync_Marquee_ShouldSelectEveryHitWithTheNearestCentreActive()
    {
        var engine = PickingEngine(new RuntimePickResult([Hit(B, 0.5f), Hit(A, 2.0f), Hit(C, 3.0f), Hit(A, 4.0f)], WorldPosition: null));
        using var sut = CreateViewport(engine.Object);
        ViewportSelection? picked = null;
        sut.SelectionPicked = selection => picked = selection;

        await sut.PickAsync(new RuntimePickRect(0, 0, 100, 50), ViewportSelectionMode.Replace, isMarquee: true).ConfigureAwait(false);

        _ = picked!.NodeIds.Should().Equal(A, C, B);
    }

    [TestMethod]
    public async Task PickAsync_WhenTheViewChangedMeanwhile_ShouldDropTheResult()
    {
        var pending = new TaskCompletionSource<RuntimePickResult?>();
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.Setup(service => service.PickViewAsync(ViewId, It.IsAny<RuntimePickRect>())).Returns(pending.Task);
        using var sut = CreateViewport(engine.Object);
        var picks = 0;
        sut.SelectionPicked = _ => picks++;

        var pick = sut.PickAsync(new RuntimePickRect(0, 0, 7, 7), ViewportSelectionMode.Replace, isMarquee: false);
        sut.AssignedViewId = new RuntimeViewId(84);
        pending.SetResult(new RuntimePickResult([Hit(A, 0.0f)], WorldPosition: null));
        await pick.ConfigureAwait(false);

        _ = picks.Should().Be(0);
    }

    [TestMethod]
    public async Task FrameSelectionAsync_ShouldFrameTheSelectedNodes()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.FrameViewAsync(ViewId, It.Is<IReadOnlyList<Guid>>(ids => ids.SequenceEqual(new[] { A, B }))))
            .ReturnsAsync(RuntimeFramingOutcome.Framed);
        using var sut = CreateViewport(engine.Object);
        sut.SelectedNodesProvider = () => [A, B];

        await sut.FrameSelectionAsync().ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.IsNoticeVisible.Should().BeFalse();
    }

    [TestMethod]
    public async Task FrameSelectionAsync_WithNothingSelected_ShouldExplainInsteadOfMoving()
    {
        using var sut = CreateViewport(new Mock<IEngineService>(MockBehavior.Strict).Object);
        sut.SelectedNodesProvider = () => [];

        await sut.FrameSelectionAsync().ConfigureAwait(false);

        _ = sut.Notice.Should().Be("Nothing selected to frame");
    }

    [TestMethod]
    public async Task FrameAllAsync_WhileLookingThroughASceneCamera_ShouldExplainTheRefusal()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.FrameViewAsync(ViewId, It.Is<IReadOnlyList<Guid>>(ids => ids.Count == 0)))
            .ReturnsAsync(RuntimeFramingOutcome.ViewingSceneCamera);
        using var sut = CreateViewport(engine.Object);

        await sut.FrameAllAsync().ConfigureAwait(false);

        _ = sut.IsNoticeVisible.Should().BeTrue();
        _ = sut.Notice.Should().Contain("editor camera");
    }

    private static RuntimePickHit Hit(Guid node, float centerDistance) => new(node, Depth: 0.5f, GeometrySlot: 0, centerDistance);

    private static Mock<IEngineService> PickingEngine(RuntimePickResult result)
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.Setup(service => service.PickViewAsync(ViewId, It.IsAny<RuntimePickRect>())).ReturnsAsync(result);
        return engine;
    }

    private static ViewportViewModel CreateViewport(IEngineService engine)
        => new(
            Guid.NewGuid(),
            engine,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance)
        {
            AssignedViewId = ViewId,
        };
}
