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
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Interop;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
[TestCategory("Viewport Camera")]
public sealed class ViewportCameraControlModeTests
{
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
                "Field of View",
                "Near View Plane",
                "Far View Plane");
        _ = menu.Items.Where(item => item.IsSeparator).Select(item => item.SeparatorLabel)
            .Should().Equal("Perspective", "Orthographic", "View");
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
        var viewId = new ViewIdManaged(43);
        engine
            .Setup(service => service.SetViewCameraMovementSpeedAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                12.0f))
            .ReturnsAsync(true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.MovementSpeed = 12.0f;

        engine.VerifyAll();
    }

    [TestMethod]
    public void ViewSettings_WhenNativeViewExists_ShouldSendSettingsToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new ViewIdManaged(44);
        engine
            .Setup(service => service.SetViewCameraSettingsAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                75.0f,
                0.1f,
                1000.0f))
            .ReturnsAsync(true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.FieldOfViewDegrees = 75.0f;

        engine.VerifyAll();
    }

    [TestMethod]
    public async Task ApplyCurrentCameraControlMode_WhenNativeViewExists_ShouldSendModeToEngine()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new ViewIdManaged(42);
        engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                CameraControlModeManaged.Fly))
            .ReturnsAsync(true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlModeManaged.Fly;

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
        var viewId = new ViewIdManaged(45);
        engine
            .Setup(service => service.SetViewCameraPresetAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                CameraViewPresetManaged.Perspective))
            .ReturnsAsync(true);
        engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                CameraControlModeManaged.Fly))
            .ReturnsAsync(true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal)).Command?.Execute(null);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Perspective);
        _ = sut.CameraControlMode.Should().Be(CameraControlModeManaged.Fly);
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal)).IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void OrthographicMenuItem_WhenCurrentModeIsFly_ShouldSwitchBackToOrbitMode()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var viewId = new ViewIdManaged(46);
        engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                CameraControlModeManaged.OrbitTurntable))
            .ReturnsAsync(true);
        engine
            .Setup(service => service.SetViewCameraPresetAsync(
                It.Is<ViewIdManaged>(id => id.Value == viewId.Value),
                CameraViewPresetManaged.Top))
            .ReturnsAsync(true);
        using var sut = CreateViewportViewModel(engine.Object);
        sut.AssignedViewId = viewId;
        sut.CameraControlMode = CameraControlModeManaged.Fly;

        sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Top", StringComparison.Ordinal)).Command?.Execute(null);

        engine.VerifyAll();
        _ = sut.CameraType.Should().Be(CameraType.Top);
        _ = sut.CameraControlMode.Should().Be(CameraControlModeManaged.OrbitTurntable);
        _ = sut.CameraMenu.Items.Single(item => string.Equals(item.Text, "Top", StringComparison.Ordinal)).IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public async Task ApplyCurrentCameraControlMode_WhenRuntimeRejectsMode_ShouldPublishWarning()
    {
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        var results = new CapturingOperationResultPublisher();
        engine
            .Setup(service => service.SetViewCameraControlModeAsync(
                It.IsAny<ViewIdManaged>(),
                CameraControlModeManaged.OrbitTrackball))
            .ReturnsAsync(false);
        using var sut = CreateViewportViewModel(engine.Object, results);
        sut.AssignedViewId = new ViewIdManaged(7);
        sut.CameraControlMode = CameraControlModeManaged.OrbitTrackball;

        await sut.ApplyCurrentCameraControlModeAsync().ConfigureAwait(false);

        var result = results.Published.Should().ContainSingle().Subject;
        _ = result.OperationKind.Should().Be(RuntimeOperationKinds.ViewSetCameraControlMode);
        _ = result.Severity.Should().Be(DiagnosticSeverity.Warning);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == DiagnosticCodes.ViewPrefix + "CAMERA_CONTROL_MODE_REJECTED");
    }

    private static ViewportViewModel CreateViewportViewModel(
        IEngineService engineService,
        IOperationResultPublisher? operationResults = null)
    {
        var appearanceSettings = new Mock<ISettingsService<IAppearanceSettings>>(MockBehavior.Loose);
        appearanceSettings
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

    private sealed class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
