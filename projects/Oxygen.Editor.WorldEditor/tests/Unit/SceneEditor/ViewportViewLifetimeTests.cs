// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

/// <summary>
/// A pane keeps its camera state across view recreation (dock move, document switch, maximize)
/// and owns the camera preview inset composed over its view.
/// </summary>
[TestClass]
[TestCategory("Viewport Camera")]
public sealed class ViewportViewLifetimeTests
{
    private static readonly SceneCameraChoice MainCamera = new(Guid.Parse("6c2e8b1f-3a4d-4f5e-9b6c-7d8e9f0a1b02"), "Main");
    private static readonly Guid Surface = Guid.Parse("0f1e2d3c-4b5a-4968-8776-655443322110");
    private static readonly RuntimeEditorCamera MovedCamera = new(
        new Vector3(4, -6, 9),
        Quaternion.CreateFromAxisAngle(Vector3.UnitZ, 0.7f),
        new Vector3(1, 2, 0),
        OrthographicSize: 7.5f);

    [TestMethod]
    public async Task RecreatedView_ShouldStartFromThePaneCameraState()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);
        await sut.OrthographicViews.Single(option => option.Label == "Top").ChooseCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        await sut.ReleaseViewAsync().ConfigureAwait(false);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);

        _ = engine.Created.Should().HaveCount(2);
        _ = engine.Created[0].EditorCamera.Should().BeNull("a new pane frames the scene");
        var recreated = engine.Created[1];
        _ = recreated.CameraPreset.Should().Be(CameraViewPreset.Top);
        _ = recreated.EditorCamera.Should().Be(MovedCamera, "the pane read its editor camera before the view was released");
        _ = recreated.SceneCameraNodeId.Should().BeNull();
        _ = sut.AssignedViewId.Should().Be(engine.Ids[1]);
    }

    [TestMethod]
    public async Task RecreatedView_ShouldLookThroughAndResumePilotingItsCamera()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);
        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);

        await sut.ReleaseViewAsync().ConfigureAwait(false);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);

        _ = engine.Created[1].SceneCameraNodeId.Should().Be(MainCamera.NodeId, "the viewed camera is part of the view's creation");
        engine.Service.Verify(service => service.SetViewScenePilotAsync(engine.Ids[1], true), Times.Once);
    }

    [TestMethod]
    public async Task InsetCamera_ShouldComposeAPreviewOverTheHostView()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);

        await sut.SetInsetCameraAsync(MainCamera).ConfigureAwait(false);

        var inset = engine.Created[1];
        _ = inset.InsetHost.Should().Be(engine.Ids[0]);
        _ = inset.CompositingTarget.Should().Be(Surface);
        _ = inset.SceneCameraNodeId.Should().Be(MainCamera.NodeId);
        _ = sut.InsetCamera.Should().Be(MainCamera);
    }

    [TestMethod]
    public async Task InsetCamera_ShouldHideWhileThePaneLooksThroughThatCamera()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);
        await sut.SetInsetCameraAsync(MainCamera).ConfigureAwait(false);

        await sut.LookThroughCameraAsync(MainCamera).ConfigureAwait(false);

        _ = engine.Destroyed.Should().Equal(engine.Ids[1]);
        _ = sut.InsetCamera.Should().BeNull();
    }

    [TestMethod]
    public async Task InsetCamera_ShouldWaitForTheHostViewAndFollowItsRecreation()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.SetInsetCameraAsync(MainCamera).ConfigureAwait(false);
        _ = engine.Created.Should().BeEmpty("an inset needs its host view");

        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);
        await sut.ReleaseViewAsync().ConfigureAwait(false);

        _ = engine.Created.Select(config => config.InsetHost).Should().Equal(null, engine.Ids[0]);
        _ = engine.Destroyed.Should().Equal([engine.Ids[1], engine.Ids[0]], "the inset goes before its host");
    }

    [TestMethod]
    public async Task Pilot_ShouldLetOtherPanesStopPilotingFirst()
    {
        var engine = new FakeEngine();
        using var sut = CreateViewport(engine.Service.Object);
        await sut.CreateViewAsync(Surface, "Pane", 640, 480).ConfigureAwait(false);
        var piloting = new List<(ViewportViewModel Pane, Guid Camera, bool PilotSent)>();
        sut.PilotStarting = (pane, camera) =>
        {
            piloting.Add((pane, camera, sut.IsPilotingSceneCamera));
            return Task.CompletedTask;
        };

        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);

        _ = piloting.Should().Equal((sut, MainCamera.NodeId, false));
        _ = sut.IsPilotingSceneCamera.Should().BeTrue();
    }

    private static ViewportViewModel CreateViewport(IEngineService engine)
    {
        return new ViewportViewModel(
            Guid.NewGuid(),
            engine,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance)
        {
            SceneCamerasProvider = () => [MainCamera],
            PilotCommitDelay = TimeSpan.Zero,
        };
    }

    /// <summary>An engine that records view creation and destruction and reports a moved editor camera.</summary>
    private sealed class FakeEngine
    {
        public FakeEngine()
        {
            _ = this.Service.SetupGet(service => service.State).Returns(EngineServiceState.Running);
            _ = this.Service.SetupGet(service => service.InputCommands).Returns(Mock.Of<IRuntimeInputCommands>());
            _ = this.Service.Setup(service => service.CreateViewAsync(It.IsAny<RuntimeViewConfig>()))
                .ReturnsAsync((RuntimeViewConfig config) =>
                {
                    this.Created.Add(config);
                    var id = new RuntimeViewId((ulong)(100 + this.Ids.Count));
                    this.Ids.Add(id);
                    return id;
                });
            _ = this.Service.Setup(service => service.DestroyViewAsync(It.IsAny<RuntimeViewId>()))
                .ReturnsAsync((RuntimeViewId id) =>
                {
                    this.Destroyed.Add(id);
                    return true;
                });
            _ = this.Service.Setup(service => service.GetViewEditorCameraAsync(It.IsAny<RuntimeViewId>())).ReturnsAsync(MovedCamera);
            _ = this.Service.Setup(service => service.SetViewCameraPresetAsync(It.IsAny<RuntimeViewId>(), It.IsAny<CameraViewPreset>())).ReturnsAsync(value: true);
            _ = this.Service.Setup(service => service.SetViewCameraControlModeAsync(It.IsAny<RuntimeViewId>(), It.IsAny<CameraControlMode>())).ReturnsAsync(value: true);
            _ = this.Service.Setup(service => service.SetViewCameraMovementSpeedAsync(It.IsAny<RuntimeViewId>(), It.IsAny<float>())).ReturnsAsync(value: true);
            _ = this.Service.Setup(service => service.SetViewCameraSettingsAsync(It.IsAny<RuntimeViewId>(), It.IsAny<float>(), It.IsAny<float>(), It.IsAny<float>())).ReturnsAsync(value: true);
            _ = this.Service.Setup(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), It.IsAny<Guid?>())).ReturnsAsync(value: true);
            _ = this.Service.Setup(service => service.SetViewScenePilotAsync(It.IsAny<RuntimeViewId>(), It.IsAny<bool>())).ReturnsAsync(value: true);
        }

        public Mock<IEngineService> Service { get; } = new(MockBehavior.Strict);

        public List<RuntimeViewConfig> Created { get; } = [];

        public List<RuntimeViewId> Ids { get; } = [];

        public List<RuntimeViewId> Destroyed { get; } = [];
    }
}
