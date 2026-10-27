// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Viewports.Unit.Tests;

/// <summary>
/// The viewport tools: tool choice and cycling, the per-user snapping and space preferences, the
/// drag readout, the property edits a gizmo or light helper drag submits, and the views the
/// orientation triad selects.
/// </summary>
[TestClass]
[TestCategory("Viewport Tools")]
internal sealed class TransformToolsTests
{
    [TestMethod]
    public void Tools_StartWithMoveAndEachCommandPicksItsTool()
    {
        using var sut = new TransformToolsViewModel(new TransformToolSettingsService(settings: null));
        _ = sut.Tool.Should().Be(RuntimeTransformTool.Translate);

        var seen = new List<RuntimeTransformTool>();
        foreach (var command in new[] { sut.UseSelectToolCommand, sut.UseRotateToolCommand, sut.UseScaleToolCommand, sut.UseMoveToolCommand })
        {
            command.Execute(parameter: null);
            seen.Add(sut.Tool);
        }

        _ = seen.Should().Equal(RuntimeTransformTool.Select, RuntimeTransformTool.Rotate, RuntimeTransformTool.Scale, RuntimeTransformTool.Translate);
    }

    [TestMethod]
    public void Tools_ReportToolSpaceAndSnapChanges()
    {
        var settings = new TransformToolSettingsService(settings: null);
        using var sut = new TransformToolsViewModel(settings);
        var changes = 0;
        sut.Changed += (_, _) => changes++;

        sut.UseScaleToolCommand.Execute(parameter: null);
        sut.ToggleSpaceCommand.Execute(parameter: null);
        sut.ToggleSnapCommand.Execute(parameter: null);
        settings.RotationIncrement = 45.0F;

        _ = changes.Should().Be(4);
        _ = sut.IsLocalSpace.Should().BeTrue();
        _ = sut.SnapEnabled.Should().BeTrue();
        _ = sut.RotationLabel.Should().Be("45°");
        _ = sut.RotationOptions.Single(option => option.IsSelected).Label.Should().Be("45°");
    }

    [TestMethod]
    public void Settings_StartFromTheDefaultsAndIgnoreInvalidIncrements()
    {
        var sut = new TransformToolSettingsService(settings: null)
        {
            TranslationIncrement = 0.0F,
            ScaleIncrement = float.NaN,
        };

        _ = sut.Snap.Should().Be(new RuntimeTransformSnap(Enabled: false, 0.25F, 15.0F, 0.1F));
        _ = sut.Space.Should().Be(RuntimeTransformSpace.World);
    }

    [TestMethod]
    public async Task Settings_PersistForTheUserAcrossProjects()
    {
        var store = new Mock<IEditorSettingsManager>();
        var sut = new TransformToolSettingsService(store.Object)
        {
            SnapEnabled = true,
            TranslationIncrement = 0.5F,
        };
        await sut.FlushAsync().ConfigureAwait(false);

        store.Verify(
            manager => manager.SaveSettingAsync(
                TransformToolSettingsService.Key,
                It.Is<TransformToolSettingsService.Preferences>(stored => stored.SnapEnabled && stored.TranslationIncrement == 0.5F),
                It.Is<SettingContext?>(context => context != null && context.Scope == SettingScope.Application),
                It.IsAny<IProgress<SettingsProgress>?>(),
                It.IsAny<CancellationToken>()),
            Times.AtLeastOnce());
    }

    [TestMethod]
    public async Task Settings_RestoreStoredPreferencesAndDiscardAnUnknownVersion()
    {
        var restored = await LoadAsync(new(TransformToolSettingsService.CurrentVersion, SnapEnabled: true, 1.0F, 90.0F, 0.5F, RuntimeTransformSpace.Local)).ConfigureAwait(false);
        var discarded = await LoadAsync(new(TransformToolSettingsService.CurrentVersion + 1, SnapEnabled: true, 1.0F, 90.0F, 0.5F, RuntimeTransformSpace.Local)).ConfigureAwait(false);

        _ = restored.Snap.Should().Be(new RuntimeTransformSnap(Enabled: true, 1.0F, 90.0F, 0.5F));
        _ = restored.Space.Should().Be(RuntimeTransformSpace.Local);
        _ = discarded.Snap.Should().Be(new RuntimeTransformSnap(Enabled: false, 0.25F, 15.0F, 0.1F));
    }

    [TestMethod]
    public void Readout_NamesTheAxesAndUnitsOfTheDrag()
    {
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Translate, RuntimeGizmoHandle.XY, 0b011, new Vector3(1.25F, -0.5F, 0))).Should().Be("X 1.250 m  Y -0.500 m");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Rotate, RuntimeGizmoHandle.Z, 0b100, new Vector3(-450.0F, 0, 0))).Should().Be("Z -450.0°");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Rotate, RuntimeGizmoHandle.View, 0, new Vector3(30.0F, 0, 0))).Should().Be("View 30.0°");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Scale, RuntimeGizmoHandle.Y, 0b010, new Vector3(1, 1.2F, 1))).Should().Be("Y 1.200×");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Scale, RuntimeGizmoHandle.Center, 0b111, new Vector3(2))).Should().Be("2.000×");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Translate, RuntimeGizmoHandle.X, 0b001, new Vector3(1, 0, 0), duplicate: true)).Should().Be("Copy  X 1.000 m");
        _ = ViewportViewModel.FormatGizmoReadout(Event(RuntimeTransformTool.Rotate, RuntimeGizmoHandle.Z, 0b100, new Vector3(10, 0, 0), representable: false)).Should().EndWith("would shear under its parent");
    }

    [TestMethod]
    public void HelperReadout_NamesTheValueAndItsUnit()
    {
        _ = ViewportViewModel.FormatHelperReadout(RuntimeHelperHandle.Range, 5.0F).Should().Be("Range 5.00 m");
        _ = ViewportViewModel.FormatHelperReadout(RuntimeHelperHandle.OuterCone, MathF.PI / 6).Should().Be("Outer cone 30.0°");
        _ = ViewportViewModel.FormatHelperReadout(RuntimeHelperHandle.InnerCone, MathF.PI / 12).Should().Be("Inner cone 15.0°");
    }

    [TestMethod]
    public void TriadAxes_LookFromTheClickedAxisAndFlipOnASecondClick()
    {
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.X, CameraType.Perspective).Should().Be(CameraType.Right);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.Y, CameraType.Top).Should().Be(CameraType.Front);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.Z, CameraType.Front).Should().Be(CameraType.Top);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.X, CameraType.Right).Should().Be(CameraType.Left);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.Y, CameraType.Front).Should().Be(CameraType.Back);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.Z, CameraType.Top).Should().Be(CameraType.Bottom);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.Z, CameraType.Bottom).Should().Be(CameraType.Top);
        _ = ViewportViewModel.AxisView(RuntimeGizmoHandle.None, CameraType.Top).Should().BeNull();
    }

    private static async Task<TransformToolSettingsService> LoadAsync(TransformToolSettingsService.Preferences stored)
    {
        var store = new Mock<IEditorSettingsManager>();
        _ = store
            .Setup(manager => manager.LoadSettingAsync(TransformToolSettingsService.Key, It.IsAny<SettingContext?>(), It.IsAny<IProgress<SettingsProgress>?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);
        var sut = new TransformToolSettingsService(store.Object);
        await sut.EnsureLoadedAsync().ConfigureAwait(false);
        return sut;
    }

    private static RuntimeGizmoEvent Event(
        RuntimeTransformTool tool,
        RuntimeGizmoHandle handle,
        int axes,
        Vector3 values,
        bool duplicate = false,
        bool representable = true)
        => new(RuntimeGizmoEventKind.Update, new RuntimeViewId(1), tool, handle, duplicate, Hovering: false, representable, [], axes, values, PivotPixel: null, Vector2.Zero);
}
