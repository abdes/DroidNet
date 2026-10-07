// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
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
    public void CameraMenu_ShouldExposeProjectionFlyAndViewSettings()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        var menu = sut.CameraMenu;

        _ = menu.Items.Select(item => item.Text)
            .Should().Equal(
                string.Empty,
                "Turntable",
                "Trackball",
                "Fly",
                "Movement Speed",
                string.Empty,
                "Top",
                "Bottom",
                "Left",
                "Right",
                "Front",
                "Back",
                string.Empty,
                "No cameras in scene",
                "Align Selected Camera to View",
                string.Empty,
                "Field of View",
                "Near View Plane",
                "Far View Plane");
        _ = menu.Items.Where(item => item.IsSeparator).Select(item => item.SeparatorLabel)
            .Should().Equal("Perspective", "Orthographic", "Scene Cameras", "View");
        _ = menu.Items.Single(item => string.Equals(item.Text, "No cameras in scene", StringComparison.Ordinal))
            .IsEnabled.Should().BeFalse();
        _ = menu.Items.Where(item => string.Equals(item.RadioGroupId, "PerspectiveCameraMode", StringComparison.Ordinal))
            .Should().HaveCount(3);
        _ = menu.Items.Where(item => string.Equals(item.RadioGroupId, "OrthographicCamera", StringComparison.Ordinal))
            .Should().HaveCount(6);
        _ = menu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal))
            .RadioGroupId.Should().Be("PerspectiveCameraMode");
        _ = menu.Items.Single(item => string.Equals(item.Text, "Turntable", StringComparison.Ordinal))
            .IsChecked.Should().BeTrue();
        _ = sut.CameraControlModeLabel.Should().Be("Turntable");
        _ = sut.CameraMenuLabel.Should().Be("Turntable");
    }

    [TestMethod]
    public void CameraMenu_ShouldExposeNumberBoxModelsForInteractiveRows()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        var menu = sut.CameraMenu;

        var movementSpeedItem = GetNumberBoxModel(menu, "Movement Speed");
        var fieldOfViewItem = GetNumberBoxModel(menu, "Field of View");
        var nearViewPlaneItem = GetNumberBoxModel(menu, "Near View Plane");
        var farViewPlaneItem = GetNumberBoxModel(menu, "Far View Plane");

        _ = movementSpeedItem.Minimum.Should().Be(1.0f);
        _ = movementSpeedItem.Maximum.Should().Be(float.PositiveInfinity);
        _ = fieldOfViewItem.Minimum.Should().Be(0.0f);
        _ = fieldOfViewItem.Maximum.Should().Be(180.0f);
        _ = fieldOfViewItem.Unit.Should().Be("\u00b0");
        _ = nearViewPlaneItem.Unit.Should().Be("m");
        _ = farViewPlaneItem.Unit.Should().Be("m");
    }

    [TestMethod]
    public void CameraNumberBoxModels_ShouldExposeRangesForNumberBoxValidation()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        var movementSpeedItem = GetNumberBoxModel(sut.CameraMenu, "Movement Speed");
        var fieldOfViewItem = GetNumberBoxModel(sut.CameraMenu, "Field of View");

        _ = movementSpeedItem.IsInRange(1.0f).Should().BeTrue();
        _ = movementSpeedItem.IsInRange(0.99f).Should().BeFalse();
        _ = fieldOfViewItem.IsInRange(180.0f).Should().BeTrue();
        _ = fieldOfViewItem.IsInRange(181.0f).Should().BeFalse();
        _ = fieldOfViewItem.IsInRange(float.NaN).Should().BeFalse();

        sut.FieldOfViewDegrees = 500.0f;

        _ = sut.FieldOfViewDegrees.Should().Be(500.0f);
    }

    [TestMethod]
    public void MovementSpeed_WhenNativeViewExists_ShouldSendSpeedToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(43);
        _ = engine
            .Setup(service => service.SetViewCameraMovementSpeedAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                12.0f))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.MovementSpeed = 12.0f;

        engine.VerifyAll();
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
    public async Task ApplyCurrentCameraControlMode_WhenNativeViewExists_ShouldSendModeToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(42);
        _ = engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraControlMode.Fly))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlMode.Fly;

        await sut.ApplyCurrentCameraControlModeAsync().ConfigureAwait(false);

        engine.VerifyAll();
        _ = sut.CameraControlModeLabel.Should().Be("Fly");
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal))
            .IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void FlyMenuItem_WhenNativeViewExists_ShouldApplyPerspectivePresetAndFlyMode()
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
                CameraControlMode.Fly))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal)).Command?.Execute(parameter: null);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Perspective);
        _ = sut.CameraControlMode.Should().Be(CameraControlMode.Fly);
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal)).IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void OrthographicMenuItem_WhenCurrentModeIsFly_ShouldSwitchBackToOrbitMode()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new RuntimeViewId(46);
        _ = engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraControlMode.OrbitTurntable))
            .ReturnsAsync(value: true);
        _ = engine
            .Setup(service => service.SetViewCameraPresetAsync(
                It.Is<RuntimeViewId>(id => id.Value == viewId.Value),
                CameraViewPreset.Top))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlMode.Fly;

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Top", StringComparison.Ordinal)).Command?.Execute(parameter: null);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Top);
        _ = sut.CameraControlMode.Should().Be(CameraControlMode.OrbitTurntable);
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Top", StringComparison.Ordinal)).IsChecked.Should().BeTrue();
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
    public void CameraMenu_ShouldListSceneCamerasUnchecked()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);
        sut.SceneCamerasProvider = () => [MainCamera, MapCamera];

        var items = sut.CameraMenu.Items.Where(item => string.Equals(item.RadioGroupId, "SceneCamera", StringComparison.Ordinal)).ToList();

        _ = items.Select(item => item.Text).Should().Equal("Main", "Map");
        _ = items.Should().OnlyContain(item => !item.IsChecked);
        _ = sut.CameraMenu.Items.Should().NotContain(item => string.Equals(item.Text, "No cameras in scene", StringComparison.Ordinal));
    }

    [TestMethod]
    public void SceneCameraMenuItem_ShouldRenderThroughThatCamera()
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

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Map", StringComparison.Ordinal)).Command?.Execute(parameter: null);

        engine.VerifyAll();
        _ = sut.SceneCamera.Should().Be(MapCamera);
        _ = sut.CameraMenuLabel.Should().Be("Map");
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Map", StringComparison.Ordinal)).IsChecked.Should().BeTrue();
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Turntable", StringComparison.Ordinal)).IsChecked.Should().BeFalse();
    }

    [TestMethod]
    public void EditorCameraMenuItem_WhenViewingSceneCamera_ShouldReturnToEditorCamera()
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
        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Main", StringComparison.Ordinal)).Command?.Execute(parameter: null);

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Top", StringComparison.Ordinal)).Command?.Execute(parameter: null);

        engine.VerifyAll();
        _ = sut.SceneCamera.Should().BeNull();
        _ = sut.CameraMenuLabel.Should().Be("Top");
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Main", StringComparison.Ordinal)).IsChecked.Should().BeFalse();
    }

    [TestMethod]
    public void RefreshCameraMenu_WhenSelectedCameraIsGone_ShouldReturnToEditorCamera()
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
        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Map", StringComparison.Ordinal)).Command?.Execute(parameter: null);
        _ = cameras.Remove(MapCamera);

        sut.RefreshCameraMenu();

        engine.Verify(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), null), Times.Once);
        _ = sut.SceneCamera.Should().BeNull();
        _ = sut.CameraMenu.Items.Where(item => string.Equals(item.RadioGroupId, "SceneCamera", StringComparison.Ordinal))
            .Select(item => item.Text).Should().Equal("Main");
    }

    [TestMethod]
    public void RefreshCameraMenu_ShouldFollowRenamedAndAddedCameras()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), MainCamera.NodeId))
            .ReturnsAsync(value: true);
        List<SceneCameraChoice> cameras = [MainCamera];
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = new RuntimeViewId(54);
        sut.SceneCamerasProvider = () => cameras;
        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Main", StringComparison.Ordinal)).Command?.Execute(parameter: null);
        var renamed = MainCamera with { Name = "Hero" };
        cameras = [renamed, MapCamera];

        sut.RefreshCameraMenu();

        _ = sut.SceneCamera.Should().Be(renamed);
        _ = sut.CameraMenuLabel.Should().Be("Hero");
        _ = sut.CameraMenu.Items.Where(item => string.Equals(item.RadioGroupId, "SceneCamera", StringComparison.Ordinal))
            .Select(item => (item.Text, item.IsChecked)).Should().Equal(("Hero", true), ("Map", false));
    }

    [TestMethod]
    public async Task ApplyCurrentSceneCamera_ShouldResendSelectionToRecreatedView()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine
            .Setup(service => service.SetViewSceneCameraAsync(It.IsAny<RuntimeViewId>(), MapCamera.NodeId))
            .ReturnsAsync(value: true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.SceneCamerasProvider = () => [MapCamera];
        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Map", StringComparison.Ordinal)).Command?.Execute(parameter: null);
        var recreated = new RuntimeViewId(55);
        sut.AssignedViewId = recreated;

        await sut.ApplyCurrentSceneCameraAsync().ConfigureAwait(false);

        engine.Verify(
            service => service.SetViewSceneCameraAsync(It.Is<RuntimeViewId>(id => id.Value == recreated.Value), MapCamera.NodeId),
            Times.Once);
    }

    private static ViewportViewModel CreateViewportViewModel(
        IEngineService engineService,
        IOperationResultPublisher? operationResults = null)
    {
        var appearanceSettings = new Mock<ISettingsService<IAppearanceSettings>>(MockBehavior.Loose);
        _ = appearanceSettings
            .SetupGet(service => service.Settings)
            .Returns(new AppearanceSettings { AppThemeMode = ElementTheme.Default });

        return new ViewportViewModel(
            Guid.NewGuid(),
            engineService,
            operationResults ?? new CapturingOperationResultPublisher(),
            new OperationStatusReducer(),
            appearanceSettings.Object,
            NullLoggerFactory.Instance);
    }

    private static ViewportCameraNumberBoxItemModel GetNumberBoxModel(IMenuSource menu, string text)
    {
        var content = menu.Items.Single(item => string.Equals(item.Text, text, StringComparison.Ordinal)).InteractiveContent;
        _ = content.Should().BeOfType<ViewportCameraNumberBoxItemModel>();
        return (ViewportCameraNumberBoxItemModel)content!;
    }

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
