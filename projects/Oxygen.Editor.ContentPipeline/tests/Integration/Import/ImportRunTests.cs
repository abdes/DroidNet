// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Models;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

/// <summary>
/// The models of one import dialog import as one run: the new ones get records with the chosen settings and cook as
/// one native batch; a replaced model's kept record, or the record of a model imported in place, takes the chosen
/// settings before it cooks; a source is analyzed before it is imported (model editor LLD section 7).
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportRunTests
{
    private static readonly AssetPath Models = AssetPath.Parse("/Content/Models");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportSources_NewModelsCookInOneRunWithTheirChosenSettings()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var external = Directory.CreateTempSubdirectory("OxygenImportRun-");
        try
        {
            var gltf = CopyFixture(external, "static_scalar_triangle.gltf", "Crate.gltf");
            var fbx = CopyFixture(external, "static_scalar_triangle.fbx", "Barrel.fbx");
            using var cook = NativeCook.Create(workspace);
            SceneImportRequest[] requests =
            [
                new(workspace.ProjectContext, gltf, "Crate", Models) { Settings = Settings("Crate", ("import_cameras", false)) },
                new(workspace.ProjectContext, fbx, "Barrel", Models) { Settings = Settings("Barrel", ("up_axis", "z")) },
            ];

            var results = await cook.Service.ImportSourcesAsync(requests, new ImportRunSettings { ThreadPoolSize = 2, MaxInFlightJobs = 2 }, this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = results.Should().ContainSingle("the new models cook as one run");
            _ = results[0].IsPublished.Should().BeTrue(string.Join(Environment.NewLine, results[0].Diagnostics.Select(static issue => issue.Message)));
            _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
            var records = new ModelImportRecords(workspace.ProjectContext.FileSystem, workspace.Root).All;
            _ = records.Should().HaveCount(2);
            _ = SettingOf(records, "Crate.gltf", "import_cameras").GetBoolean().Should().BeFalse();
            _ = SettingOf(records, "Barrel.fbx", "up_axis").GetString().Should().Be("z");
            _ = results[0].CookedAssets.Select(static asset => asset.SourceAssetUri).Distinct().Should().BeEquivalentTo(
                [AssetPath.Parse("/Content/Models/Crate/Crate.gltf"), AssetPath.Parse("/Content/Models/Barrel/Barrel.fbx")]);
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportSources_AReplacedModelKeepsItsRecordAndTakesTheChosenSettings()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var external = Directory.CreateTempSubdirectory("OxygenImportReplace-");
        try
        {
            var source = CopyFixture(external, "static_scalar_triangle.gltf", "Model.gltf");
            using var cook = NativeCook.Create(workspace);
            var first = await cook.Service.ImportSourceAsync(new(workspace.ProjectContext, source, "Model", Models), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = first.IsPublished.Should().BeTrue();
            var before = new ModelImportRecords(workspace.ProjectContext.FileSystem, workspace.Root).All.Single();
            var review = await SceneImportReplacement.ReviewAsync(workspace.ProjectContext, Models, "Model", this.TestContext.CancellationToken).ConfigureAwait(false);

            var results = await cook.Service.ImportSourcesAsync(
                [new(workspace.ProjectContext, source, "Model", Models) { Replacement = review, Settings = Settings("Model", ("unit_policy", "preserve")) }],
                ImportRunSettings.None,
                this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = results.Should().ContainSingle();
            _ = results[0].Status.Should().BeOneOf(OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings);
            var after = new ModelImportRecords(workspace.ProjectContext.FileSystem, workspace.Root).All.Single();
            _ = after.RecordPath.Should().Be(before.RecordPath, "a replaced model keeps its record");
            _ = after.Settings.SourceIdentity.Should().Be(before.Settings.SourceIdentity);
            _ = after.Settings.Settings.GetProperty("unit_policy").GetString().Should().Be("preserve");
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportSources_AModelImportedInPlaceIsImportedAgainWithTheChosenSettings()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var folder = Directory.CreateDirectory(Path.Combine(workspace.Root, "Content", "Models"));
        var source = CopyFixture(folder, "static_scalar_triangle.gltf", "Crate.gltf");
        using var cook = NativeCook.Create(workspace);
        var first = await cook.Service.ImportSourcesAsync([new(workspace.ProjectContext, source, "Crate", Models)], ImportRunSettings.None, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.Should().ContainSingle().Which.IsPublished.Should().BeTrue();
        var before = new ModelImportRecords(workspace.ProjectContext.FileSystem, workspace.Root).All.Single();

        var results = await cook.Service.ImportSourcesAsync(
            [new(workspace.ProjectContext, source, "Crate", Models) { Settings = Settings("Crate", ("unit_policy", "preserve")) }],
            ImportRunSettings.None,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = results.Should().ContainSingle();
        _ = results[0].Status.Should().BeOneOf([OperationStatus.Succeeded, OperationStatus.SucceededWithWarnings], string.Join(Environment.NewLine, results[0].Diagnostics.Select(static issue => issue.Message)));
        var after = new ModelImportRecords(workspace.ProjectContext.FileSystem, workspace.Root).All;
        _ = after.Should().ContainSingle("the model keeps its one record").Which.RecordPath.Should().Be(before.RecordPath);
        _ = after[0].Settings.Settings.GetProperty("unit_policy").GetString().Should().Be("preserve");
    }

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task Analyze_ReadsTheFactsOfASourceBeforeItIsImported()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        using var cook = NativeCook.Create(workspace);
        var analyzer = new ModelSourceAnalyzer(cook.Api, new ContentImportManifestBuilder());
        var source = Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.fbx");

        var analysis = await analyzer.AnalyzeAsync(workspace.ProjectContext, source, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = analysis.Complete.Should().BeTrue(string.Join(Environment.NewLine, analysis.Diagnostics.Select(static issue => issue.Message)));
        _ = analysis.Facts.Should().NotBeNull();
        _ = analysis.Facts!.Format.Should().Be("fbx");
        _ = analysis.Facts.MeshCount.Should().BePositive();
        var scratch = Path.Combine(workspace.Root, ".build", "import-analysis");
        _ = (!Directory.Exists(scratch) || !Directory.EnumerateFileSystemEntries(scratch).Any()).Should().BeTrue("the analysis leaves no scratch files");
    }

    [TestMethod]
    public async Task Analyze_AMissingSourceIsReportedNotThrown()
    {
        using var workspace = new CookWorkspace();
        var analyzer = new ModelSourceAnalyzer(Mock.Of<IEngineContentPipelineApi>(), new ContentImportManifestBuilder());

        var analysis = await analyzer.AnalyzeAsync(workspace.ProjectContext, Path.Combine(workspace.Root, "Missing.fbx"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = analysis.Complete.Should().BeFalse();
        _ = analysis.Facts.Should().BeNull();
        _ = analysis.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(AssetImportDiagnosticCodes.SourceMissing);
    }

    private static string CopyFixture(DirectoryInfo folder, string fixture, string name)
    {
        var path = Path.Combine(folder.FullName, name);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", fixture), path);
        return path;
    }

    private static JsonElement Settings(string name, params (string Key, object Value)[] values)
        => JsonSerializer.SerializeToElement(values.Prepend(("name", name)).ToDictionary(static value => value.Key, static value => value.Value, StringComparer.Ordinal));

    private static JsonElement SettingOf(IEnumerable<ModelImportRecord> records, string file, string key)
        => records.Single(record => string.Equals(Path.GetFileName(record.SourcePath), file, StringComparison.Ordinal)).Settings.Settings.GetProperty(key);
}
