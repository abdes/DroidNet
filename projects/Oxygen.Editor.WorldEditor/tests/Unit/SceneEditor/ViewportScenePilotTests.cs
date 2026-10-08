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
/// Piloting a scene camera and aligning a camera to the view: runtime requests, menu state, and
/// one undoable commit per navigation gesture.
/// </summary>
[TestClass]
[TestCategory("Viewport Camera")]
public sealed class ViewportScenePilotTests
{
    private static readonly SceneCameraChoice MainCamera = new(Guid.Parse("4b1d7a9e-2f0c-4c7e-8d3a-6a9f0e5b1c01"), "Main");
    private static readonly RuntimeViewId ViewId = new(61);
    private static readonly RuntimeViewCameraPose Pose = new(new Vector3(1, 2, 3), new Vector3(10, 20, 30), Vector3.One, OrthographicSize: null);

    [TestMethod]
    public async Task TogglePilot_ShouldPilotTheViewedCamera()
    {
        var engine = CreateEngine();
        using var sut = CreateViewport(engine.Object);
        await sut.LookThroughCameraAsync(MainCamera).ConfigureAwait(false);

        await sut.TogglePilotCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        engine.Verify(service => service.SetViewScenePilotAsync(It.Is<RuntimeViewId>(id => id.Value == ViewId.Value), true), Times.Once);
        _ = sut.IsPilotingSceneCamera.Should().BeTrue();
        _ = sut.CameraMenuLabel.Should().Be("Piloting Main");
        _ = sut.PilotLabel.Should().Be("Stop piloting");
    }

    [TestMethod]
    public void Pilot_ShouldOnlyBeAvailableWhileViewingASceneCamera()
    {
        using var sut = CreateViewport(CreateEngine().Object);

        _ = sut.CanPilot.Should().BeFalse();
        _ = sut.PilotHint.Should().Be("Look through a scene camera to pilot it.");
    }

    [TestMethod]
    public async Task PilotGesture_ShouldCommitOnceAfterInputIsReleased()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);

        sut.NotifyNavigationInput(inputHeld: true);
        sut.NotifyNavigationInput(inputHeld: true);
        _ = commits.Should().BeEmpty("a held button or key is still navigating");
        sut.NotifyNavigationInput(inputHeld: false);
        await WaitForAsync(() => commits.Count == 1).ConfigureAwait(false);

        _ = commits.Should().ContainSingle().Which.Should().Be((MainCamera.NodeId, Pose));
    }

    [TestMethod]
    public async Task StopPiloting_ShouldCommitThePendingGestureFirst()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.PilotCommitDelay = TimeSpan.FromHours(1);
        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);
        sut.NotifyNavigationInput(inputHeld: false);

        await sut.StopPilotingAsync().ConfigureAwait(false);

        _ = commits.Should().ContainSingle();
        _ = sut.IsPilotingSceneCamera.Should().BeFalse();
        _ = sut.SceneCamera.Should().Be(MainCamera, "stopping the pilot keeps looking through the camera");
        engine.Verify(service => service.SetViewScenePilotAsync(It.IsAny<RuntimeViewId>(), false), Times.Once);
    }

    [TestMethod]
    public async Task ReturnToEditorCamera_WhilePiloting_ShouldCommitAndEndThePilot()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.PilotCommitDelay = TimeSpan.FromHours(1);
        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);
        sut.NotifyNavigationInput(inputHeld: false);

        await sut.ReturnToEditorCameraAsync().ConfigureAwait(false);

        _ = commits.Should().ContainSingle();
        _ = sut.IsPilotingSceneCamera.Should().BeFalse();
        _ = sut.SceneCamera.Should().BeNull();
    }

    [TestMethod]
    public async Task NavigationWithoutPilot_ShouldNotCommit()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.PilotCommitDelay = TimeSpan.Zero;
        await sut.LookThroughCameraAsync(MainCamera).ConfigureAwait(false);

        sut.NotifyNavigationInput(inputHeld: false);
        await Task.Delay(50).ConfigureAwait(false);

        _ = commits.Should().BeEmpty();
        engine.Verify(service => service.GetViewCameraPoseAsync(It.IsAny<RuntimeViewId>(), It.IsAny<Guid>()), Times.Never);
    }

    [TestMethod]
    public async Task AlignCameraToView_ShouldCommitTheEditorCameraPose()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.SelectedCameraProvider = () => MainCamera;

        sut.RefreshSceneCameras();
        _ = sut.AlignCameraLabel.Should().Be("Align 'Main' to view");
        _ = sut.CanAlignCamera.Should().BeTrue();
        await sut.AlignSelectedCameraToViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = commits.Should().ContainSingle().Which.Should().Be((MainCamera.NodeId, Pose));
    }

    [TestMethod]
    public async Task AlignCameraToView_WhileLookingThroughACamera_ShouldDoNothing()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.SelectedCameraProvider = () => MainCamera;
        await sut.LookThroughCameraAsync(MainCamera).ConfigureAwait(false);

        var aligned = await sut.AlignCameraToViewAsync(MainCamera).ConfigureAwait(false);

        _ = aligned.Should().BeFalse();
        _ = commits.Should().BeEmpty();
        _ = sut.CanAlignCamera.Should().BeFalse();
        _ = sut.AlignCameraDisabledReason.Should().Be("Return to the editor camera to align a camera to it.");
    }

    [TestMethod]
    public async Task LockedCamera_ShouldNotBePilotedOrAligned()
    {
        var engine = CreateEngine();
        var commits = new List<(Guid NodeId, RuntimeViewCameraPose Pose)>();
        using var sut = CreateViewport(engine.Object, commits);
        sut.CameraLockProvider = _ => true;
        sut.SelectedCameraProvider = () => MainCamera;

        _ = (await sut.AlignCameraToViewAsync(MainCamera).ConfigureAwait(false)).Should().BeFalse();
        await sut.PilotCameraAsync(MainCamera).ConfigureAwait(false);

        _ = sut.IsPilotingSceneCamera.Should().BeFalse();
        _ = sut.CanPilot.Should().BeFalse();
        _ = commits.Should().BeEmpty();
        engine.Verify(service => service.SetViewScenePilotAsync(It.IsAny<RuntimeViewId>(), It.IsAny<bool>()), Times.Never);
    }

    private static Mock<IEngineService> CreateEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.Setup(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), It.IsAny<Guid?>())).ReturnsAsync(value: true);
        _ = engine.Setup(service => service.SetViewScenePilotAsync(It.IsAny<RuntimeViewId>(), It.IsAny<bool>())).ReturnsAsync(value: true);
        _ = engine.Setup(service => service.SetViewCameraPresetAsync(It.IsAny<RuntimeViewId>(), It.IsAny<CameraViewPreset>())).ReturnsAsync(value: true);
        _ = engine.Setup(service => service.GetViewCameraPoseAsync(It.IsAny<RuntimeViewId>(), It.IsAny<Guid>())).ReturnsAsync(Pose);
        return engine;
    }

    private static ViewportViewModel CreateViewport(IEngineService engine, List<(Guid NodeId, RuntimeViewCameraPose Pose)>? commits = null)
    {
        var viewport = new ViewportViewModel(
            Guid.NewGuid(),
            engine,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance)
        {
            AssignedViewId = ViewId,
            SceneCamerasProvider = () => [MainCamera],
            PilotCommitDelay = TimeSpan.Zero,
            CameraPoseCommitter = (nodeId, pose) =>
            {
                commits?.Add((nodeId, pose));
                return Task.FromResult(true);
            },
        };
        return viewport;
    }

    private static async Task WaitForAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (!condition())
        {
            await Task.Delay(10, timeout.Token).ConfigureAwait(false);
        }
    }
}
