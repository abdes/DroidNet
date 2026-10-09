// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

[TestClass]
[TestCategory("Viewport Camera")]
public sealed partial class ViewportCameraControlModeTests
{
    private static readonly SceneCameraChoice MainCamera = new(Guid.Parse("8d0f3a52-5c1e-4b8e-9a51-3f2b1c6d7e01"), "Main");
    private static readonly SceneCameraChoice MapCamera = new(Guid.Parse("8d0f3a52-5c1e-4b8e-9a51-3f2b1c6d7e02"), "Map");

    [TestMethod]
    public void CameraFlyout_ShouldOfferPerspectiveModesAndOrthographicDirections()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        _ = sut.PerspectiveModes.Select(option => option.Label).Should().Equal("Turntable", "Trackball");
        _ = sut.PerspectiveModes.Should().OnlyContain(option => option.HasDescription);
        _ = sut.OrthographicViews.Select(option => option.Label).Should().Equal("Top", "Bottom", "Front", "Back", "Left", "Right");
        _ = SelectedLabels(sut).Should().Equal("Turntable");
        _ = sut.HasSceneCameras.Should().BeFalse();
        _ = sut.CameraControlModeLabel.Should().Be("Turntable");
        _ = sut.CameraMenuLabel.Should().Be("Turntable");
        _ = sut.IsPerspectiveView.Should().BeTrue();
    }

    [TestMethod]
    public void CameraNumberFields_ShouldExposeRangesForNumberBoxValidation()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        _ = sut.MovementSpeedField.Minimum.Should().Be(1.0f);
        _ = sut.MovementSpeedField.Maximum.Should().Be(float.PositiveInfinity);
        _ = sut.MovementSpeedField.IsInRange(1.0f).Should().BeTrue();
        _ = sut.MovementSpeedField.IsInRange(0.99f).Should().BeFalse();
        _ = sut.FieldOfViewField.Unit.Should().Be("°");
        _ = sut.FieldOfViewField.IsInRange(180.0f).Should().BeTrue();
        _ = sut.FieldOfViewField.IsInRange(181.0f).Should().BeFalse();
        _ = sut.FieldOfViewField.IsInRange(float.NaN).Should().BeFalse();
        _ = sut.NearViewPlaneField.Unit.Should().Be("m");
        _ = sut.FarViewPlaneField.Unit.Should().Be("m");
        _ = sut.ClippingSummary.Should().Contain("0.1").And.Contain("1000").And.EndWith(" m");
    }

    [TestMethod]
    public void FlySpeed_ShouldClampToTheMinimumAndSendSpeedToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(43);
        _ = engine
            .Setup(service => service.SetViewCameraMovementSpeedAsync(It.Is<RuntimeViewId>(id => id.Value == viewId.Value), It.IsAny<float>()))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.FlySpeed = 12.0;
        _ = sut.MovementSpeed.Should().Be(12.0f);
        sut.FlySpeed = 0.25;

        _ = sut.MovementSpeed.Should().Be(1.0f);
        engine.Verify(service => service.SetViewCameraMovementSpeedAsync(It.IsAny<RuntimeViewId>(), 12.0f), Times.Once);
        engine.Verify(service => service.SetViewCameraMovementSpeedAsync(It.IsAny<RuntimeViewId>(), 1.0f), Times.Once);
    }

    [TestMethod]
    public void ViewSettings_WhenNativeViewExists_ShouldSendSettingsToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(44);
        _ = engine
            .Setup(service => service.SetViewCameraSettingsAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                75.0f,
                0.1f,
                1000.0f))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.FieldOfViewDegrees = 75.0f;

        engine.VerifyAll();
    }

    [TestMethod]
    public void ResetLens_ShouldRestoreLensDefaultsOnly()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Loose);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.CameraControlMode = CameraControlMode.OrbitTrackball;
        sut.MovementSpeed = 7.0f;
        sut.FieldOfViewDegrees = 40.0f;
        sut.NearViewPlane = 2.0f;
        sut.FarViewPlane = 20.0f;

        sut.ResetLensCommand.Execute(parameter: null);

        _ = sut.FieldOfViewDegrees.Should().Be(90.0f);
        _ = sut.NearViewPlane.Should().Be(0.1f);
        _ = sut.FarViewPlane.Should().Be(1000.0f);
        _ = sut.MovementSpeed.Should().Be(7.0f);
        _ = sut.CameraControlMode.Should().Be(CameraControlMode.OrbitTrackball);
    }

    [TestMethod]
    public async Task ApplyCurrentCameraControlMode_WhenNativeViewExists_ShouldSendModeToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(42);
        _ = engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraControlMode.OrbitTrackball))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlMode.OrbitTrackball;

        await sut.ApplyCurrentCameraControlModeAsync().ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.CameraControlModeLabel.Should().Be("Trackball");
        _ = SelectedLabels(sut).Should().Equal("Trackball");
    }

    [TestMethod]
    public void StepFlySpeed_ScalesBy25PercentPerTickWithinTheFieldBounds()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Loose).Object);
        sut.MovementSpeed = 4.0f;

        sut.StepFlySpeed(1.0f);
        _ = sut.MovementSpeed.Should().BeApproximately(5.0f, 1e-4f);
        sut.StepFlySpeed(-2.0f);
        _ = sut.MovementSpeed.Should().BeApproximately(3.2f, 1e-4f);
        sut.StepFlySpeed(-20.0f);
        _ = sut.MovementSpeed.Should().Be(1.0f, "the speed field's minimum bounds the wheel");
        sut.StepFlySpeed(1000.0f);
        _ = sut.MovementSpeed.Should().Be(10000.0f, "the wheel keeps the speed finite");
    }

    [TestMethod]
    public void CanFly_OnlyForAPerspectiveEditorCameraOrAPilotedSceneCamera()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Loose).Object);
        _ = sut.CanFly.Should().BeTrue();

        sut.CameraType = CameraType.Top;
        _ = sut.CanFly.Should().BeFalse("orthographic views do not fly");

        sut.CameraType = CameraType.Perspective;
        sut.SceneCamera = new SceneCameraChoice(Guid.NewGuid(), "Shot");
        _ = sut.CanFly.Should().BeFalse("looking through a scene camera does not move it");
    }

    [TestMethod]
    public async Task TrackballOption_WhenNativeViewExists_ShouldApplyPerspectivePresetAndTrackballMode()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(45);
        _ = engine
            .Setup(service => service.SetViewCameraPresetAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraViewPreset.Perspective))
            .ReturnsAsync(value: true);
        _ = engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraControlMode.OrbitTrackball))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        await Choose(sut.PerspectiveModes, "Trackball").ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Perspective);
        _ = sut.CameraControlMode.Should().Be(CameraControlMode.OrbitTrackball);
        _ = SelectedLabels(sut).Should().Equal("Trackball");
    }

    [TestMethod]
    public async Task OrthographicOption_KeepsTheOrbitStyle()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(46);
        _ = engine
            .Setup(service => service.SetViewCameraPresetAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraViewPreset.Top))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlMode.OrbitTrackball;

        await Choose(sut.OrthographicViews, "Top").ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Top);
        _ = sut.CameraControlMode.Should().Be(CameraControlMode.OrbitTrackball);
        _ = sut.IsPerspectiveView.Should().BeFalse();
        _ = SelectedLabels(sut).Should().Equal("Top");
    }

    [TestMethod]
    public async Task ApplyCurrentCameraControlMode_WhenRuntimeRejectsMode_ShouldPublishWarning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var results = new CapturingOperationResultPublisher();
        _ = engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.IsAny<RuntimeViewId>(),
                CameraControlMode.OrbitTrackball))
            .ReturnsAsync(value: false);
        using var sut = CreateViewportViewModel(engine.Object, results);
        sut.AssignedViewId = new RuntimeViewId(7);
        sut.CameraControlMode = CameraControlMode.OrbitTrackball;

        await sut.ApplyCurrentCameraControlModeAsync().ConfigureAwait(false);

        var result = results.Published.Should().ContainSingle().Subject;
        _ = result.OperationKind.Should().Be(RuntimeOperationKinds.ViewSetCameraControlMode);
        _ = result.Severity.Should().Be(DiagnosticSeverity.Warning);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == DiagnosticCodes.ViewPrefix + "CAMERA_CONTROL_MODE_REJECTED");
    }

    [TestMethod]
    public void RefreshSceneCameras_ShouldListSceneCamerasUnselected()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);
        sut.SceneCamerasProvider = () => [MainCamera, MapCamera];

        sut.RefreshSceneCameras();

        _ = sut.SceneCameraOptions.Select(option => option.Label).Should().Equal("Main", "Map");
        _ = sut.SceneCameraOptions.Should().OnlyContain(option => !option.IsSelected);
        _ = sut.HasSceneCameras.Should().BeTrue();
    }

    [TestMethod]
    public async Task SceneCameraOption_ShouldRenderThroughThatCamera()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(51);
        _ = engine
            .Setup(service => service.SetViewSceneCameraAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                MapCamera.NodeId))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.SceneCamerasProvider = () => [MainCamera, MapCamera];
        sut.RefreshSceneCameras();

        await Choose(sut.SceneCameraOptions, "Map").ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.SceneCamera.Should().Be(MapCamera);
        _ = sut.CameraMenuLabel.Should().Be("Map");
        _ = SelectedLabels(sut).Should().Equal("Map");
    }

    [TestMethod]
    public async Task OrthographicOption_WhenViewingSceneCamera_ShouldReturnToEditorCamera()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(52);
        var sequence = new MockSequence();
        _ = engine.InSequence(sequence)
            .Setup(service => service.SetViewSceneCameraAsync(It.Is<RuntimeViewId>(id => id.Value == viewId.Value), MainCamera.NodeId))
            .ReturnsAsync(value: true);
        _ = engine.InSequence(sequence)
            .Setup(service => service.SetViewSceneCameraAsync(It.Is<RuntimeViewId>(id => id.Value == viewId.Value), null))
            .ReturnsAsync(value: true);
        _ = engine.InSequence(sequence)
            .Setup(service => service.SetViewCameraPresetAsync(It.Is<RuntimeViewId>(id => id.Value == viewId.Value), CameraViewPreset.Top))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.SceneCamerasProvider = () => [MainCamera];
        sut.RefreshSceneCameras();
        await Choose(sut.SceneCameraOptions, "Main").ConfigureAwait(false);

        await Choose(sut.OrthographicViews, "Top").ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.SceneCamera.Should().BeNull();
        _ = sut.CameraMenuLabel.Should().Be("Top");
        _ = SelectedLabels(sut).Should().Equal("Top");
    }

    [TestMethod]
    public async Task RefreshSceneCameras_WhenViewedCameraIsGone_ShouldReturnToEditorCamera()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(53);
        _ = engine
            .Setup(service => service.SetViewSceneCameraAsync(It.Is<RuntimeViewId>(id => id.Value == viewId.Value), It.IsAny<Guid?>()))
            .ReturnsAsync(value: true);
        List<SceneCameraChoice> cameras = [MainCamera, MapCamera];
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.SceneCamerasProvider = () => cameras;
        sut.RefreshSceneCameras();
        await Choose(sut.SceneCameraOptions, "Map").ConfigureAwait(false);
        _ = cameras.Remove(MapCamera);

        sut.RefreshSceneCameras();

        engine.Verify(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), null), Times.Once);
        _ = sut.SceneCamera.Should().BeNull();
        _ = sut.SceneCameraOptions.Select(option => option.Label).Should().Equal("Main");
    }

    [TestMethod]
    public async Task RefreshSceneCameras_ShouldFollowRenamedAndAddedCameras()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), MainCamera.NodeId))
            .ReturnsAsync(value: true);
        List<SceneCameraChoice> cameras = [MainCamera];
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = new RuntimeViewId(54);
        sut.SceneCamerasProvider = () => cameras;
        sut.RefreshSceneCameras();
        await Choose(sut.SceneCameraOptions, "Main").ConfigureAwait(false);
        var renamed = MainCamera with { Name = "Hero" };
        cameras = [renamed, MapCamera];

        sut.RefreshSceneCameras();

        _ = sut.SceneCamera.Should().Be(renamed);
        _ = sut.CameraMenuLabel.Should().Be("Hero");
        _ = sut.SceneCameraOptions.Select(option => (option.Label, option.IsSelected)).Should().Equal(("Hero", true), ("Map", false));
    }

    private static ViewportViewModel CreateViewportViewModel(
        IEngineService engineService,
        IOperationResultPublisher? operationResults = null)
        => new(
            Guid.NewGuid(),
            engineService,
            operationResults ?? new CapturingOperationResultPublisher(),
            new OperationStatusReducer(),
            NullLoggerFactory.Instance);

    private static Task Choose(IEnumerable<ViewportOption> options, string label)
        => options.Single(option => string.Equals(option.Label, label, StringComparison.Ordinal)).ChooseCommand.ExecuteAsync(parameter: null);

    private static IEnumerable<string> SelectedLabels(ViewportViewModel viewport)
        => viewport.PerspectiveModes
            .Concat(viewport.OrthographicViews)
            .Concat(viewport.SceneCameraOptions)
            .Where(option => option.IsSelected)
            .Select(option => option.Label);

    private sealed class CapturingOperationResultPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoopDisposable();
    }

    private sealed partial class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
