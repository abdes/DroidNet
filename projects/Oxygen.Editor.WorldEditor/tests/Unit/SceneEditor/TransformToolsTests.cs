// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.SceneEditor;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

/// <summary>
/// The viewport tools: tool choice and cycling, the per-user snapping and space preferences, the
/// drag readout, the property edits a gizmo or light helper drag submits, and the views the
/// orientation triad selects.
/// </summary>
[TestClass]
[TestCategory("Viewport Tools")]
public sealed class TransformToolsTests
{
    private static readonly Guid NodeId = Guid.NewGuid();

    [TestMethod]
    public void Tools_StartWithMoveAndCycleThroughAllFour()
    {
        using var sut = new TransformToolsViewModel(new TransformToolSettingsService(settings: null));
        _ = sut.Tool.Should().Be(RuntimeTransformTool.Translate);

        var seen = new List<RuntimeTransformTool>();
        for (var i = 0; i < 4; i++)
        {
            sut.CycleToolCommand.Execute(parameter: null);
            seen.Add(sut.Tool);
        }

        _ = seen.Should().Equal(RuntimeTransformTool.Rotate, RuntimeTransformTool.Scale, RuntimeTransformTool.Select, RuntimeTransformTool.Translate);
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
        var sut = new TransformToolSettingsService(settings: null);

        sut.TranslationIncrement = 0.0F;
        sut.ScaleIncrement = float.NaN;

        _ = sut.Snap.Should().Be(new RuntimeTransformSnap(false, 0.25F, 15.0F, 0.1F));
        _ = sut.Space.Should().Be(RuntimeTransformSpace.World);
    }

    [TestMethod]
    public async Task Settings_PersistForTheUserAcrossProjects()
    {
        var store = new Mock<IEditorSettingsManager>();
        var sut = new TransformToolSettingsService(store.Object);

        sut.SnapEnabled = true;
        sut.TranslationIncrement = 0.5F;
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
        var restored = await LoadAsync(new(TransformToolSettingsService.CurrentVersion, true, 1.0F, 90.0F, 0.5F, RuntimeTransformSpace.Local)).ConfigureAwait(false);
        var discarded = await LoadAsync(new(TransformToolSettingsService.CurrentVersion + 1, true, 1.0F, 90.0F, 0.5F, RuntimeTransformSpace.Local)).ConfigureAwait(false);

        _ = restored.Snap.Should().Be(new RuntimeTransformSnap(true, 1.0F, 90.0F, 0.5F));
        _ = restored.Space.Should().Be(RuntimeTransformSpace.Local);
        _ = discarded.Snap.Should().Be(new RuntimeTransformSnap(false, 0.25F, 15.0F, 0.1F));
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
    public void GizmoEdits_ChangeOnlyWhatTheToolMoves()
    {
        var target = new RuntimeGizmoTarget(NodeId, new Vector3(1, 2, 3), Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI / 2), new Vector3(2, 3, 4));
        var transform = SceneDocumentCommandService.Transform;

        var move = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Translate, [target])[NodeId];
        var rotate = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Rotate, [target])[NodeId];
        var scale = SceneEditorViewModel.BuildGizmoEdits(RuntimeTransformTool.Scale, [target])[NodeId];

        _ = move.Ids.Should().BeEquivalentTo([transform.PositionX.Id, transform.PositionY.Id, transform.PositionZ.Id]);
        _ = rotate.Count.Should().Be(6);
        _ = rotate.GetTyped(transform.RotationZ, out var degrees).Should().BeTrue();
        _ = degrees.Should().BeApproximately(90.0F, 1.0e-3F);
        _ = scale.GetTyped(transform.ScaleY, out var scaleY).Should().BeTrue();
        _ = scaleY.Should().Be(3.0F);
        _ = scale.Contains(transform.RotationX.Id).Should().BeFalse();
    }

    [TestMethod]
    public void HelperEdits_ChangeOnlyTheDraggedLightValue()
    {
        var scene = new Scene(new Mock<IProject>().Object) { Name = "Scene" };
        var spot = new SceneNode(scene) { Name = "Spot" };
        _ = spot.AddComponent(new SpotLightComponent { Name = "Spot" });
        var point = new SceneNode(scene) { Name = "Point" };
        _ = point.AddComponent(new PointLightComponent { Name = "Point" });
        var empty = new SceneNode(scene) { Name = "Empty" };

        var outer = SceneEditorViewModel.BuildHelperEdit(spot, RuntimeHelperHandle.OuterCone, 0.6F);
        var range = SceneEditorViewModel.BuildHelperEdit(point, RuntimeHelperHandle.Range, 7.5F);

        _ = outer!.Ids.Should().BeEquivalentTo([SceneDocumentCommandService.SpotLight.OuterConeAngleRadiansDescriptor!.Id]);
        _ = outer.GetTyped(new PropertyId<float>(SceneDocumentCommandService.SpotLight.OuterConeAngleRadiansDescriptor!.Id), out var radians).Should().BeTrue();
        _ = radians.Should().Be(0.6F);
        _ = range!.Ids.Should().BeEquivalentTo([SceneDocumentCommandService.PointLight.RangeDescriptor.Id]);
        _ = SceneEditorViewModel.BuildHelperEdit(point, RuntimeHelperHandle.InnerCone, 0.2F).Should().BeNull();
        _ = SceneEditorViewModel.BuildHelperEdit(empty, RuntimeHelperHandle.Range, 1.0F).Should().BeNull();
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
