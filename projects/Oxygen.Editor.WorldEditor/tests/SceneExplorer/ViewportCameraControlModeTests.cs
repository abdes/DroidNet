// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Interop;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
[TestCategory("Viewport Camera")]
public sealed class ViewportCameraControlModeTests
{
    [TestMethod]
    public void CameraControlMenu_ShouldExposeTurntableTrackballAndFlyModes()
    {
        using var sut = CreateViewportViewModel(new Mock<IEngineService>(MockBehavior.Strict).Object);

        var menu = sut.CameraControlMenu;

        _ = menu.Items.Select(item => item.Text)
            .Should().Equal("Orbit - Turntable", "Orbit - Trackball", "Fly");
        _ = menu.Items.Should().OnlyContain(item => item.RadioGroupId == "CameraControlMode");
        _ = menu.Items.Single(item => string.Equals(item.Text, "Orbit - Turntable", StringComparison.Ordinal))
            .IsChecked.Should().BeTrue();
        _ = sut.CameraControlModeLabel.Should().Be("Orbit");
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
        _ = sut.CameraControlMenu.Items.Single(item => string.Equals(item.Text, "Fly", StringComparison.Ordinal))
            .IsChecked.Should().BeTrue();
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
