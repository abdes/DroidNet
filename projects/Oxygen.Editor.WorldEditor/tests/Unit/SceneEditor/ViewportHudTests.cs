// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

/// <summary>
/// The viewport heads-up display: view modes, Show options, statistics, the layout picker, the
/// navigation hint and the pane state they persist.
/// </summary>
[TestClass]
[TestCategory("Viewport HUD")]
public sealed class ViewportHudTests
{
    private static readonly RuntimeViewId ViewId = new(71);

    [TestMethod]
    public void ViewModeGroups_ShouldOfferShadingLightingAndBufferViews()
    {
        using var sut = CreateViewport(new Mock<IEngineService>(MockBehavior.Strict).Object);

        _ = sut.ViewModeGroups.Select(group => group.Title).Should().Equal(null, "Lighting", "Buffer visualization");
        _ = sut.ViewModeGroups.SelectMany(group => group.Options).Select(option => option.Label).Should().Equal(
            "Lit",
            "Unlit",
            "Wireframe",
            "Lit + wireframe",
            "Direct",
            "Indirect",
            "World normals",
            "Roughness",
            "Metalness",
            "Scene depth",
            "Shadow mask");
        _ = SelectedViewModes(sut).Should().Equal("Lit");
        _ = sut.ViewModeLabel.Should().Be("Lit");
    }

    [TestMethod]
    public async Task ViewModeOption_ShouldApplyToThePaneViewAndPersist()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewRenderOptionsAsync(It.Is<RuntimeViewId>(id => id.Value == ViewId.Value), ViewportViewMode.Roughness, true))
            .ReturnsAsync(value: true);
        using var sut = CreateViewport(engine.Object);
        sut.AssignedViewId = ViewId;
        var stateChanges = 0;
        sut.StateChanged += (_, _) => stateChanges++;

        await sut.ViewModeGroups.SelectMany(group => group.Options).Single(option => option.Label == "Roughness")
            .ChooseCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.ViewMode.Should().Be(ViewportViewMode.Roughness);
        _ = sut.ViewModeLabel.Should().Be("Roughness");
        _ = SelectedViewModes(sut).Should().Equal("Roughness");
        _ = stateChanges.Should().Be(1);
    }

    [TestMethod]
    public void ShowGrid_ShouldSendTheGridWithTheCurrentViewMode()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewRenderOptionsAsync(It.Is<RuntimeViewId>(id => id.Value == ViewId.Value), ViewportViewMode.Lit, false))
            .ReturnsAsync(value: true);
        using var sut = CreateViewport(engine.Object);
        sut.AssignedViewId = ViewId;

        sut.ShowGrid = false;

        engine.VerifyAll();
    }

    [TestMethod]
    public void ViewMode_WhenRuntimeRejectsIt_ShouldPublishWarning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewRenderOptionsAsync(It.IsAny<RuntimeViewId>(), ViewportViewMode.Wireframe, true))
            .ReturnsAsync(value: false);
        var results = new Mock<IOperationResultPublisher>();
        using var sut = CreateViewport(engine.Object, results.Object);
        sut.AssignedViewId = ViewId;

        sut.ViewMode = ViewportViewMode.Wireframe;

        results.Verify(
            publisher => publisher.Publish(It.Is<OperationResult>(result =>
                result.OperationKind == RuntimeOperationKinds.ViewSetRenderOptions
                && result.Severity == DiagnosticSeverity.Warning)),
            Times.Once);
    }

    [TestMethod]
    public void RefreshStatistics_ShouldReportFrameRateFrameTimeAndNodeCount()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.Setup(service => service.GetFrameStatistics()).Returns(new RuntimeFrameStatistics(59.6f, 16.78f));
        using var sut = CreateViewport(engine.Object);
        sut.NodeCountProvider = () => 42;

        sut.ShowStatistics = true;

        _ = sut.StatisticsText.Should().Be(string.Create(System.Globalization.CultureInfo.CurrentCulture, $"60 fps · {16.8:0.0} ms · 42 nodes"));
    }

    [TestMethod]
    public void RefreshStatistics_WhenEngineIsNotRunning_ShouldReportOnlyTheScene()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.Setup(service => service.GetFrameStatistics()).Returns((RuntimeFrameStatistics?)null);
        using var sut = CreateViewport(engine.Object);
        sut.NodeCountProvider = () => 1;

        sut.RefreshStatistics();

        _ = sut.StatisticsText.Should().Be("1 node");
    }

    [TestMethod]
    public void LayoutGroups_ShouldOfferEveryLayoutOnceAndMarkTheCurrentOne()
    {
        using var sut = CreateViewport(new Mock<IEngineService>(MockBehavior.Strict).Object);
        SceneViewLayout? requested = null;
        sut.OnLayoutRequested = layout => requested = layout;

        sut.CurrentLayout = SceneViewLayout.ThreeMainLeft;
        var options = sut.LayoutGroups.SelectMany(group => group.Options).ToList();
        options.Single(option => option.Layout == SceneViewLayout.FourQuad).ChooseCommand.Execute(parameter: null);

        _ = options.Select(option => option.Layout).Should().BeEquivalentTo(Enum.GetValues<SceneViewLayout>());
        _ = options.Where(option => option.IsSelected).Select(option => option.Layout).Should().Equal(SceneViewLayout.ThreeMainLeft);
        _ = requested.Should().Be(SceneViewLayout.FourQuad);
    }

    [TestMethod]
    public void GestureHint_ShouldHideAfterNavigationAndReturnWhenTheCameraChanges()
    {
        using var sut = CreateViewport(new Mock<IEngineService>(MockBehavior.Loose).Object);
        sut.NavigationSettleDelay = TimeSpan.FromHours(1);
        _ = sut.IsGestureHintVisible.Should().BeFalse("only the focused pane shows the hint");

        sut.IsFocused = true;
        _ = sut.IsGestureHintVisible.Should().BeTrue();
        _ = sut.GestureHint.Should().StartWith("Alt+drag orbit");

        sut.NotifyNavigationInput(inputHeld: true);
        _ = sut.IsGestureHintVisible.Should().BeFalse();

        sut.CameraControlMode = CameraControlMode.Fly;
        _ = sut.IsGestureHintVisible.Should().BeTrue();
        _ = sut.GestureHint.Should().StartWith("Right-drag look");

        sut.CameraType = CameraType.Top;
        _ = sut.GestureHint.Should().Be("Alt+middle-drag pan · wheel zoom");
    }

    [TestMethod]
    public void CaptureState_ShouldRoundTripTheViewOptions()
    {
        using var source = CreateViewport(new Mock<IEngineService>(MockBehavior.Loose).Object);
        source.ViewMode = ViewportViewMode.LitWireframe;
        source.ShowGrid = false;
        source.ShowCameraPreview = false;
        source.ShowStatistics = true;
        source.CameraType = CameraType.Front;

        var state = source.CaptureState();
        using var restored = CreateViewport(new Mock<IEngineService>(MockBehavior.Loose).Object);
        restored.RestoreState(state, sceneCamera: null);

        _ = restored.CaptureState().Should().Be(state);
        _ = state.Should().Be(new ViewportPaneState(
            CameraType.Front,
            CameraControlMode.OrbitTurntable,
            EditorCamera: null,
            SceneCameraId: null,
            ViewportViewMode.LitWireframe,
            ShowGrid: false,
            ShowCameraPreview: false,
            ShowStatistics: true));
    }

    private static ViewportViewModel CreateViewport(IEngineService engine, IOperationResultPublisher? results = null)
        => new(
            Guid.NewGuid(),
            engine,
            results ?? Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance);

    private static IEnumerable<string> SelectedViewModes(ViewportViewModel viewport)
        => viewport.ViewModeGroups.SelectMany(group => group.Options).Where(option => option.IsSelected).Select(option => option.Label);
}
