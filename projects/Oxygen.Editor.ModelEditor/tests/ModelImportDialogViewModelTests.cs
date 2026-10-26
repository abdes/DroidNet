// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ModelEditor.TestSupport.ImportDialogs;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// The Import model dialog's file list: files are added, removed and reordered, a taken name or a missing file blocks
/// the import, and the dialog returns each file with its name, folder and settings in the list's order (model editor LLD
/// section 7).
/// </summary>
[TestClass]
public sealed class ModelImportDialogViewModelTests
{
    [TestMethod]
    public void AddFiles_ListsModelsOnceAndNamesThemFromTheirFile()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb", "Paint.png");
        using var dialog = Create(project);

        var added = dialog.AddFiles([sources[0], sources[1], sources[2], sources[0]]);

        _ = added.Select(static file => file.Name).Should().Equal("Car", "Truck");
        _ = dialog.Files.Should().HaveCount(2);
        _ = dialog.Files.Should().AllSatisfy(file => file.DestinationFolder.Should().Be(Destination.VirtualPath));
        _ = dialog.HasNotice.Should().BeTrue("a file that is not a model is left out with a note");
        _ = dialog.CanImport.Should().BeTrue();
    }

    [TestMethod]
    public void AddFiles_SelectsTheFilesAddedThoughNoViewListens()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);

        _ = dialog.AddFiles([sources[0]]);
        _ = dialog.Selection.Should().Equal(dialog.Files[0]);

        _ = dialog.AddFiles([sources[1]]);
        _ = dialog.Selection.Should().Equal(new[] { dialog.Files[1] }, "a view that loads later shows the dialog's selection");
    }

    [TestMethod]
    public void ANameTakenInTheProjectBlocksTheImportUntilReplacedOrRenamed()
    {
        var (project, sources) = CreateProject("Car.fbx");
        _ = project.FileSystem.Directory.CreateDirectory(Path.Combine(project.ProjectRoot.FullPath, "Content", "Vehicles", "Car"));
        using var dialog = Create(project);

        var file = dialog.AddFiles(sources).Single();
        dialog.UpdateSelection([file]);

        _ = file.IsNameTaken.Should().BeTrue();
        _ = file.Status.Should().Be(ModelImportFileStatus.Error);
        _ = (dialog.HasProblems, dialog.Problems).Should().Be((true, "A model named Car is already in /Content/Vehicles. Replace it, or choose another name."), "the dialog says what blocks the import");
        _ = dialog.HasNamesToReplace.Should().BeTrue();
        _ = dialog.CanImport.Should().BeFalse();
        _ = dialog.ReplaceCommand.CanExecute(parameter: null).Should().BeTrue();

        dialog.SelectedName = "Coupe";

        _ = file.IsNameTaken.Should().BeFalse("another name is free");
        _ = dialog.CanImport.Should().BeTrue();
        _ = dialog.HasProblems.Should().BeFalse();
    }

    [TestMethod]
    public void AModelImportedInPlaceIsImportedAgainFromTheSettingsOfItsLastImport()
    {
        var (project, _) = CreateProject();
        var root = project.ProjectRoot.FullPath;
        var source = Path.Combine(root, "Content", "Scenes", "BurgerPiz.glb");
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        project.FileSystem.File.WriteAllText(source, "model");
        var record = ModelImportSettings.PathFor(root, Guid.CreateVersion7());
        _ = project.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(record)!);
        using (var settings = JsonDocument.Parse("""{"name":"BurgerPiz","unit_policy":"preserve"}"""))
        {
            project.FileSystem.File.WriteAllBytes(record, ModelImportSettings.Create(record, source, "/Content/Scenes/BurgerPiz.glb", settings.RootElement).ToBytes());
        }

        using var dialog = Create(project);
        var file = dialog.AddFiles([source]).Single();
        dialog.UpdateSelection([file]);

        _ = (file.IsReimport, file.Status == ModelImportFileStatus.Error, dialog.CanImport, dialog.HasProblems).Should().Be((true, false, true, false), "an imported model is imported again, not refused");
        _ = Setting(file, "unit_policy")!.Value.GetString().Should().Be("preserve", "the settings start from its last import");
        _ = dialog.ReimportNote.Should().Be("BurgerPiz.glb is already imported. Import imports it again with these settings, which start from its last import.");
    }

    [TestMethod]
    public void TwoFilesOfOneNameInOneFolderBlockTheImport()
    {
        var (project, sources) = CreateProject("Car.fbx", @"Old\Car.fbx");
        using var dialog = Create(project);

        _ = dialog.AddFiles(sources);

        _ = dialog.Files[1].HasNameProblem.Should().BeTrue();
        _ = dialog.CanImport.Should().BeFalse();

        dialog.UpdateSelection([dialog.Files[1]]);
        dialog.SelectedFolder = "/Content/Archive";

        _ = dialog.CanImport.Should().BeTrue();
    }

    [TestMethod]
    public void AMissingSourceBlocksTheImport()
    {
        var (project, sources) = CreateProject("Car.fbx");
        using var dialog = Create(project);
        var file = dialog.AddFiles(sources).Single();

        project.FileSystem.File.Delete(sources[0]);

        _ = dialog.Validate().Should().BeFalse();
        _ = file.IsMissing.Should().BeTrue();
        _ = file.Status.Should().Be(ModelImportFileStatus.Error);
    }

    [TestMethod]
    public void MoveCommandsAreDisabledAtTheListsEnds()
    {
        var (project, sources) = CreateProject("A.fbx", "B.fbx", "C.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);

        dialog.UpdateSelection([dialog.Files[0]]);
        _ = dialog.MoveUpCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = dialog.MoveDownCommand.CanExecute(parameter: null).Should().BeTrue();

        dialog.UpdateSelection([dialog.Files[1], dialog.Files[2]]);
        _ = dialog.MoveDownCommand.CanExecute(parameter: null).Should().BeFalse();

        dialog.MoveUpCommand.Execute(parameter: null);

        _ = dialog.Files.Select(static file => file.Name).Should().Equal("B", "C", "A");
    }

    [TestMethod]
    public void RemoveTakesTheSelectionOut()
    {
        var (project, sources) = CreateProject("A.fbx", "B.fbx", "C.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([dialog.Files[0], dialog.Files[2]]);

        dialog.RemoveCommand.Execute(parameter: null);

        _ = dialog.Files.Select(static file => file.Name).Should().Equal("B");
    }

    [TestMethod]
    public void RemovingTheLastFileKeepsTheListCommandsAnswerable()
    {
        var (project, sources) = CreateProject("A.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        var answers = 0;
        foreach (var command in new[] { dialog.RemoveCommand, dialog.MoveUpCommand, dialog.MoveDownCommand })
        {
            // As a bound button does: each refresh asks the command again, while the list changes.
            command.CanExecuteChanged += (_, _) =>
            {
                _ = command.CanExecute(parameter: null);
                ++answers;
            };
        }

        dialog.RemoveCommand.Execute(parameter: null);

        _ = dialog.Files.Should().BeEmpty();
        _ = answers.Should().BePositive();
        _ = (dialog.RemoveCommand.CanExecute(parameter: null), dialog.MoveUpCommand.CanExecute(parameter: null)).Should().Be((false, false));
    }

    [TestMethod]
    public void ToPlan_ReturnsEveryFileInOrderWithItsNamedSettingsAndTheRun()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([dialog.Files[1]]);
        dialog.SelectedName = "Hauler";
        dialog.Run.Threads.NumberValue = 4;

        var plan = dialog.ToPlan();

        _ = plan.Models.Select(static model => model.Name).Should().Equal("Car", "Hauler");
        _ = plan.Models[1].Settings.GetProperty(ModelImportSettingCatalog.NameKey).GetString().Should().Be("Hauler");
        _ = plan.Models.Should().AllSatisfy(model => model.DestinationFolder.Should().Be(Destination));
        _ = plan.Models.Should().AllSatisfy(static model => model.Replaces.Should().BeNull());
        _ = plan.Run.ThreadPoolSize.Should().Be(4);
    }

    [TestMethod]
    public void TheFactsLineWordsTheSelectedFilesAnalysis()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project, new Dictionary<string, ModelSourceFacts>(StringComparer.Ordinal) { ["Car.fbx"] = Facts("fbx", "Paint", "Glass") });
        _ = dialog.AddFiles(sources);

        dialog.UpdateSelection([dialog.Files[0]]);
        _ = dialog.Facts.Should().Be("FBX 7.4 · centimetres, Z up, right-handed · 14 nodes · 9 meshes · 2 materials · 0 textures");

        dialog.UpdateSelection([.. dialog.Files]);
        _ = dialog.Facts.Should().Be("2 files selected", "the facts describe one file; with several the bar counts them");
        _ = dialog.BeforeImport.Should().BeEmpty("neither file has a finding");
        _ = dialog.HasReport.Should().BeFalse();
    }

    [TestMethod]
    public void TheReportCountsTheSelectedFilesFindingsAndTakesTheWorstSeverity()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb", "Crates.gltf");
        var findings = new Dictionary<string, ImmutableArray<DiagnosticRecord>>(StringComparer.Ordinal)
        {
            ["Car.fbx"] = [Finding(DiagnosticSeverity.Warning, "Animations are not imported."), Finding(DiagnosticSeverity.Info, "Below the report.")],
            ["Truck.glb"] = [Finding(DiagnosticSeverity.Error, "The file could not be read.")],
        };
        using var dialog = Create(project, findings: findings);
        _ = dialog.AddFiles(sources);

        dialog.UpdateSelection([dialog.Files[0]]);
        _ = (dialog.HasReport, dialog.ReportCount, dialog.ReportSeverity, dialog.ReportSummary).Should().Be((true, 1, DiagnosticSeverity.Warning, "1 warning"));
        _ = dialog.BeforeImport.Should().ContainSingle().Which.ShowsHeader.Should().BeFalse();

        dialog.UpdateSelection([.. dialog.Files]);
        _ = (dialog.ReportCount, dialog.ReportSeverity, dialog.ReportSummary).Should().Be((2, DiagnosticSeverity.Error, "1 error, 1 warning"));
        _ = dialog.BeforeImport.Select(static group => group.FileName).Should().Equal(new[] { "Car.fbx", "Truck.glb" }, "a file without findings is left out");

        dialog.UpdateSelection([dialog.Files[2]]);
        _ = (dialog.HasReport, dialog.ReportSummary).Should().Be((false, string.Empty));
    }

    [TestMethod]
    public async Task ASessionSavedByTheDialogOpensWithItsFilesSettingsAndRun()
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        var manifest = Path.Combine(project.ProjectRoot.FullPath, "Vehicles.import-manifest.json");
        using (var dialog = Create(project))
        {
            _ = dialog.AddFiles(sources);
            dialog.UpdateSelection([dialog.Files[0]]);
            dialog.Settings.UpAxis.SelectedOption = dialog.Settings.UpAxis.Options.Single(static option => Equals(option.Value, "z"));
            dialog.Run.JobsAtOnce.NumberValue = 3;
            await dialog.SaveSessionFileAsync(manifest, CancellationToken.None).ConfigureAwait(false);
        }

        using var reopened = Create(project);
        var added = await reopened.OpenSessionFileAsync(manifest, CancellationToken.None).ConfigureAwait(false);

        _ = added.Select(static file => file.SourcePath).Should().Equal(sources);
        _ = added.Select(static file => file.DestinationFolder).Should().AllBe(Destination.VirtualPath);
        _ = ModelImportSettingCatalog.UpAxis.ReadFrom(added[0].Settings).Should().Be("z");
        _ = reopened.Run.JobsAtOnce.Value.Should().Be(3);
        _ = reopened.Run.Threads.IsSet.Should().BeFalse();
    }

    [TestMethod]
    public void ContentFolderOf_IsTheFoldersPathUnderItsMount()
    {
        var (project, _) = CreateProject();
        using var dialog = Create(project);

        _ = dialog.ContentFolderOf(Path.Combine(project.ProjectRoot.FullPath, "Content", "Props")).Should().Be("/Content/Props");
        _ = dialog.ContentFolderOf(@"D:\Elsewhere").Should().BeNull();
        _ = AssetPath.Parse(dialog.ContentFolderOf(Path.Combine(project.ProjectRoot.FullPath, "Content"))!).Should().Be(AssetPath.Content);
    }
}
