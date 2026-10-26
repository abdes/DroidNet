// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Controls;
using Oxygen.Editor.Models;
using static Oxygen.Editor.ModelEditor.TestSupport.ImportDialogs;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// The import settings panel edits the selected files as the Properties pane edits a multi-selection: shared values show,
/// differing ones are mixed, the override bar is full or half, and a row edits only the files it applies to, saying
/// which (model editor LLD section 7, Settings; ME-13).
/// </summary>
[TestClass]
public sealed class ModelImportSettingsPanelViewModelTests
{
    [TestMethod]
    public void AnEditAppliesToEverySelectedFile_AndAResetRemovesIt()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([.. dialog.Files]);
        var panel = dialog.Settings;

        panel.SmoothingAngle.NumberValue = 45;

        _ = dialog.Files.Should().AllSatisfy(static file => ModelImportSettingCatalog.SmoothingAngle.ReadFrom(file.Settings).Should().Be(45d));
        _ = panel.SmoothingAngle.OverrideState.Should().Be(OverrideState.Full);

        panel.SmoothingAngle.ResetCommand.Execute(parameter: null);

        _ = dialog.Files.Should().AllSatisfy(static file => ModelImportSettingCatalog.SmoothingAngle.ReadFrom(file.Settings).Should().BeNull());
        _ = panel.SmoothingAngle.OverrideState.Should().Be(OverrideState.None);
        _ = panel.SmoothingAngle.NumberValue.Should().Be(60, "an unset value shows the importer's default");
    }

    [TestMethod]
    public void DifferingValuesAreMixed_AndSomeSetIsAHalfOverride()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([dialog.Files[0]]);
        dialog.Settings.ImportLights.Value = false;

        dialog.UpdateSelection([.. dialog.Files]);

        _ = dialog.Settings.ImportLights.IsMixed.Should().BeTrue();
        _ = dialog.Settings.ImportLights.Value.Should().BeNull("a mixed toggle shows its Mixed state");
        _ = dialog.Settings.ImportLights.OverrideState.Should().Be(OverrideState.Partial);
    }

    [TestMethod]
    public void AFormatsRowEditsOnlyItsFiles_AndItsQualifierSaysWhich()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([.. dialog.Files]);
        var upAxis = dialog.Settings.UpAxis;

        upAxis.SelectedOption = upAxis.Options.Single(static option => Equals(option.Value, "y"));

        _ = upAxis.IsVisible.Should().BeTrue();
        _ = upAxis.Qualifier.Should().Be("FBX files");
        _ = ModelImportSettingCatalog.UpAxis.ReadFrom(dialog.Files[0].Settings).Should().Be("y");
        _ = ModelImportSettingCatalog.UpAxis.ReadFrom(dialog.Files[1].Settings).Should().BeNull("glTF is Y up by specification");

        dialog.UpdateSelection([dialog.Files[1]]);
        _ = upAxis.IsVisible.Should().BeFalse("a row that does not apply to the file is absent");
    }

    [TestMethod]
    public void ScaleShowsWithCustomUnits()
    {
        var (project, sources) = CreateProject("Car.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([.. dialog.Files]);
        var panel = dialog.Settings;
        _ = panel.UnitScale.IsVisible.Should().BeFalse();

        panel.UnitPolicy.SelectedOption = panel.UnitPolicy.Options.Single(static option => Equals(option.Value, "custom"));

        _ = panel.UnitScale.IsVisible.Should().BeTrue();
        panel.UnitScale.NumberValue = 0;
        _ = panel.UnitScale.HasError.Should().BeTrue("the scale's range is the manifest schema's");
        _ = ModelImportSettingCatalog.UnitScale.ReadFrom(dialog.Files[0].Settings).Should().BeNull();
    }

    [TestMethod]
    public void KeepNodePivotsIsTheInverseOfBakingTransforms()
    {
        var (project, sources) = CreateProject("Car.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([.. dialog.Files]);

        _ = dialog.Settings.KeepNodePivots.Value.Should().BeTrue("pivots are kept by default");

        dialog.Settings.KeepNodePivots.Value = false;

        _ = ModelImportSettingCatalog.BakeTransforms.ReadFrom(dialog.Files[0].Settings).Should().Be(true);
    }

    [TestMethod]
    public void SceneAndMaterialsShowForOneFile()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project, new Dictionary<string, ModelSourceFacts>(StringComparer.Ordinal)
        {
            ["Car.fbx"] = Facts("fbx", "Paint", "Glass"),
            ["Truck.glb"] = Facts("glb") with
            {
                Scenes = [new(0, "Day", "Day", IsDefault: true, 1, 10), new(1, string.Empty, "#1", IsDefault: false, 1, 4)],
            },
        });
        _ = dialog.AddFiles(sources);

        dialog.UpdateSelection([dialog.Files[0]]);
        _ = dialog.Settings.Materials.Select(static row => row.Material).Should().Equal("Paint", "Glass");
        _ = dialog.Settings.HasScenes.Should().BeFalse();

        dialog.Settings.Materials[0].Remap("/Content/Materials/CarPaint.omat");
        _ = ModelMaterialRemap.Read(dialog.Files[0].Settings).Should().ContainKey("Paint");
        _ = dialog.Settings.Materials[0].OverrideState.Should().Be(OverrideState.Full);

        dialog.UpdateSelection([dialog.Files[1]]);
        _ = dialog.Settings.HasScenes.Should().BeTrue();
        _ = dialog.Settings.Scene.Options.Select(static option => option.Value).Should().Equal(new object?[] { null, "Day", "#1" });

        dialog.UpdateSelection([.. dialog.Files]);
        _ = dialog.Settings.Materials.Should().BeEmpty();
        _ = dialog.Settings.HasScenes.Should().BeFalse();
        _ = dialog.Settings.ShowsSelectOneNote.Should().BeTrue();
    }

    [TestMethod]
    public void TheRunShowsDefaultsUntilSet_AndResetAllReturnsToThem()
    {
        var (project, _) = CreateProject();
        using var dialog = Create(project);
        var run = dialog.Run;

        _ = run.Pipelines[0].Workers.NumberValue.Should().Be(2);
        _ = run.HasCustomSettings.Should().BeFalse();

        run.Pipelines[0].Workers.NumberValue = 4;

        _ = run.HasCustomSettings.Should().BeTrue();
        _ = run.ToSettings().Concurrency["texture"].Should().Be(new ImportPipelineSettings(4, null));

        run.ResetAllCommand.Execute(parameter: null);

        _ = run.ToSettings().IsEmpty.Should().BeTrue();
    }
}
