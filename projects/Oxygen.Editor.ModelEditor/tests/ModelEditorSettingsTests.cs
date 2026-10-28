// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using AwesomeAssertions;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Documents;
using Oxygen.Editor.ModelEditor.Settings;
using Oxygen.Editor.ModelEditor.TestSupport;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// The import settings panel beside the Model Editor's viewport (model editor 5.3, ME-5, ME-6, M3 to M5, M10): each edit
/// is one undo step that dirties the document, Save replaces the record through the content pipeline, the toolbar counts the
/// model's kept problems, and the Selected mesh and Selected texture groups follow the selection.
/// </summary>
[TestClass]
public sealed class ModelEditorSettingsTests : IDisposable
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");
    private static readonly AssetPath BodyGeometry = AssetPath.Parse("/Content/Car.glb/Body.ogeo");
    private static readonly Guid GeometryKey = Guid.Parse("4d7c1a2b-3e4f-4a5b-8c6d-7e8f9a0b1c2d");
    private static readonly Guid PaintSurface = Guid.Parse("0f6d5c1e-8a40-4c5e-9b8e-3c1f2a6b7d90");
    private static readonly Guid PaintMaterial = Guid.Parse("9a8b7c6d-5e4f-4a3b-9c2d-1e0f9a8b7c6d");

    private readonly LateRecordWriter records = new();
    private readonly TestSceneDocuments documents;
    private readonly TestModelEditorServices services;

    public ModelEditorSettingsTests()
    {
        this.documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()), modelRecords: this.records);
        this.services = new TestModelEditorServices(this.documents, this.records);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task AnEditInThePanelIsOneUndoStepThatDirtiesTheDocument()
    {
        _ = LateRecordWriter.WriteRecord(this.documents, Car, """{"name":"Car","unit_policy":"normalize"}""");
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);

        editor.Settings.UnitPolicy.SelectedOption = editor.Settings.UnitPolicy.Options.Single(static option => Equals(option.Value, "custom"));

        _ = editor.HasImportSettings.Should().BeTrue();
        _ = car.IsDirty.Should().BeTrue("Save writes the settings and reimports");
        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(ModelOf(car).Record!.Settings).Should().Be("custom");
        _ = car.History.UndoStack.Should().ContainSingle();
        await car.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = editor.Settings.UnitPolicy.SelectedOption!.Value.Should().Be("normalize");
        _ = car.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task ReimportWithUnsavedSettingsSavesThemAndImportsWithThem()
    {
        var path = this.RecordPathOf(LateRecordWriter.WriteRecord(this.documents, Car));
        byte[]? written = null;
        _ = this.services.ContentPipeline
            .Setup(value => value.ReplaceModelRecordAsync(Car, It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .Callback((AssetPath _, ReadOnlyMemory<byte> _, ReadOnlyMemory<byte> replacement, ProjectContext _, CancellationToken _) =>
            {
                written = replacement.ToArray();
                this.documents.Project.Context.FileSystem.File.WriteAllBytes(path, written);
            })
            .ReturnsAsync(Succeeded());
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        editor.Settings.ImportCameras.Value = false;

        await editor.ReimportCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = car.IsDirty.Should().BeFalse("the reimport saved the settings it imports with");
        _ = ModelImportSettingCatalog.ImportCameras.ReadFrom(ModelImportSettings.Parse(written!).Settings).Should().Be(false);
        this.services.ContentPipeline.Verify(value => value.ReimportSourceAsync(It.IsAny<AssetPath>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()), Times.Never(), "saving the settings is the import");
    }

    [TestMethod]
    public async Task ReimportWithoutEditsImportsTheSavedSettingsAgain()
    {
        _ = LateRecordWriter.WriteRecord(this.documents, Car);
        _ = this.services.ContentPipeline
            .Setup(value => value.ReimportSourceAsync(Car, It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(Succeeded());
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);

        await editor.ReimportCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = car.IsDirty.Should().BeFalse();
        this.services.ContentPipeline.Verify(value => value.ReimportSourceAsync(Car, It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()), Times.Once());
        this.services.ContentPipeline.Verify(value => value.ReplaceModelRecordAsync(It.IsAny<AssetPath>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()), Times.Never());
    }

    [TestMethod]
    public async Task SaveReplacesTheRecordThroughTheContentPipeline()
    {
        var path = this.RecordPathOf(LateRecordWriter.WriteRecord(this.documents, Car));
        byte[]? written = null;
        _ = this.services.ContentPipeline
            .Setup(value => value.ReplaceModelRecordAsync(Car, It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .Callback((AssetPath _, ReadOnlyMemory<byte> _, ReadOnlyMemory<byte> replacement, ProjectContext _, CancellationToken _) =>
            {
                written = replacement.ToArray();
                this.documents.Project.Context.FileSystem.File.WriteAllBytes(path, written);
            })
            .ReturnsAsync(Succeeded());
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        editor.Settings.ImportCameras.Value = false;

        var result = await car.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Saved);
        _ = car.IsDirty.Should().BeFalse();
        _ = ModelImportSettingCatalog.ImportCameras.ReadFrom(ModelImportSettings.Parse(written!).Settings).Should().Be(false);
        this.services.Results.Verify(value => value.Publish(It.Is<OperationResult>(published => published.Title == "Save import settings")), Times.Once);
    }

    [TestMethod]
    public async Task ClosingWithUnsavedSettingsCanSaveThem()
    {
        var path = this.RecordPathOf(LateRecordWriter.WriteRecord(this.documents, Car));
        _ = this.services.ContentPipeline
            .Setup(value => value.ReplaceModelRecordAsync(Car, It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .Callback((AssetPath _, ReadOnlyMemory<byte> _, ReadOnlyMemory<byte> replacement, ProjectContext _, CancellationToken _) =>
                this.documents.Project.Context.FileSystem.File.WriteAllBytes(path, replacement.ToArray()))
            .ReturnsAsync(Succeeded());
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        editor.Settings.ImportCameras.Value = false;
        IDocumentCloseParticipant participant = editor;

        await participant.PrepareForCloseAsync().ConfigureAwait(false);
        var saved = await participant.SaveForCloseAsync().ConfigureAwait(false);

        _ = saved.Should().BeTrue("a model with unsaved settings is closed as any document is: saved, discarded or kept open");
        _ = car.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task ASaveOverARecordThatChangedConflicts()
    {
        var record = LateRecordWriter.WriteRecord(this.documents, Car);
        var path = this.RecordPathOf(record);
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        editor.Settings.ImportCameras.Value = false;
        this.documents.Project.Context.FileSystem.File.WriteAllBytes(path, (record with { PublishedNodes = [new("Body", Guid.NewGuid())] }).ToBytes());
        _ = this.services.ContentPipeline
            .Setup(value => value.ReplaceModelRecordAsync(Car, It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(Succeeded() with { Status = OperationStatus.Failed });

        var result = await car.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Conflict);
        _ = car.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task ACookTakesTheRewrittenRecordAndKeepsUnsavedEdits()
    {
        var record = LateRecordWriter.WriteRecord(this.documents, Car);
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        editor.Settings.ImportCameras.Value = false;
        var node = new ModelPublishedNode("Body", Guid.NewGuid());
        this.documents.Project.Context.FileSystem.File.WriteAllBytes(this.RecordPathOf(record), (record with { PublishedNodes = [node] }).ToBytes());

        // The handler takes the arguments alone, not a sender and arguments.
        object[] completed = [new CookCompletedEventArgs(this.documents.Project.Context, Succeeded())];
        this.services.CookRuns.Raise(value => value.CookCompleted += null, completed);

        _ = ModelOf(car).Record!.PublishedNodes.Should().Equal(node);
        _ = ModelImportSettingCatalog.ImportCameras.ReadFrom(ModelOf(car).Record!.Settings).Should().Be(false, "the unsaved edit stays");
        _ = editor.Settings.ImportCameras.Value.Should().BeFalse();
    }

    [TestMethod]
    public async Task TheToolbarCountsTheKeptProblemsAndShowsThemInProblems()
    {
        _ = LateRecordWriter.WriteRecord(this.documents, Car);
        _ = this.services.Published
            .Setup(value => value.ReadDiagnosticsAsync(It.IsAny<ProjectContext>(), Car, It.IsAny<CancellationToken>()))
            .ReturnsAsync([Warning("Paint_Normal.png is missing."), Warning("The animation 'Drive' is dropped.")]);
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        await this.services.Problems.ReadAsync(car).ConfigureAwait(false);
        var problems = new Mock<IProblemReveal>();
        editor.AttachWindow(new DocumentWindowServices(new WindowId(1), new TestDocumentArea(car), Mock.Of<ICommandService>(), Mock.Of<IPlacementService>(), Reveal: null) { Problems = problems.Object });

        await editor.ShowProblemsCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = editor.ProblemCount.Should().Be(2);
        _ = editor.ProblemCountText.Should().Be("2 problems");
        problems.Verify(value => value.ShowFileAsync(car.File.Path.FullPath), Times.Once);
    }

    [TestMethod]
    public async Task AKeptProblemNamingANodeSelectsItAndIsListedUnderTheModel()
    {
        var door = new ModelPublishedNode("Body/Door_FL", Guid.NewGuid());
        _ = LateRecordWriter.WriteRecord(this.documents, Car, change: record => record with { PublishedNodes = [new("Body", Guid.NewGuid()), door] });
        _ = this.services.Published
            .Setup(value => value.ReadDiagnosticsAsync(It.IsAny<ProjectContext>(), Car, It.IsAny<CancellationToken>()))
            .ReturnsAsync([Warning("Door_FL has no normals.") with { AffectedEntity = new AffectedScope { SourceObjectName = "Door_FL" } }]);
        var car = await this.documents.OpenModelAsync(Car).ConfigureAwait(false);

        await this.services.Problems.ReadAsync(car).ConfigureAwait(false);

        var problem = this.services.Problems.ProblemsOf(car).Should().ContainSingle().Which.Should().BeOfType<CookProblem>().Subject;
        _ = problem.File.Should().Be(car.File.Path.FullPath);
        _ = problem.Issue.SuggestedAction!.Payload["NodeReference"].Should().Be(this.services.Scenes.ReferenceOf(car, NodeId.FromValue(door.NodeId))!.ToString());
        this.documents.Open.Close(car);
        _ = this.services.Problems.Problems.Should().BeEmpty("a closed model's problems leave the pane");
    }

    [TestMethod]
    public async Task ContinueMakesANewNodeContinueAVanishedAnchorAsAnUnsavedEntry()
    {
        var front = new ModelPublishedNode("Body/Door_Front_L", Guid.NewGuid());
        _ = LateRecordWriter.WriteRecord(this.documents, Car, change: record => record with
        {
            PublishedNodes = [new("Body", Guid.NewGuid()), front],
            NewNodes = [front],
            VanishedNodes = [new("Body/Door_FL", Guid.NewGuid())],
        });
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        var group = editor.Settings.Continuity!;
        var vanished = group.Vanished.Should().ContainSingle().Subject;
        _ = group.Summary.Should().Be("The last import: 1 node vanished, 1 is new. No match is guessed.");

        vanished.Prepare();
        _ = vanished.Targets.Select(static target => target.Anchor).Should().Equal("Body/Door_Front_L", "Body");
        vanished.Chosen = vanished.Targets[0];
        vanished.ContinueCommand.Execute(parameter: null);

        group = editor.Settings.Continuity!;
        _ = group.Vanished.Single().Status.Should().Be("continued by Body/Door_Front_L");
        _ = group.Entries.Should().ContainSingle().Which.OverrideState.Should().Be(OverrideState.Full);
        _ = group.HasUnsaved.Should().BeTrue();
        _ = car.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task ForgetDropsAVanishedAnchorUntilSave()
    {
        _ = LateRecordWriter.WriteRecord(this.documents, Car, change: record => record with { VanishedNodes = [new("world", Guid.NewGuid())] });
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);

        editor.Settings.Continuity!.Vanished.Single().ForgetCommand.Execute(parameter: null);

        _ = editor.Settings.Continuity!.Vanished.Should().BeEmpty();
        _ = editor.Settings.Continuity.HasUnsaved.Should().BeTrue("a forgotten anchor applies on Save");
        _ = ModelOf(car).Record!.VanishedNodes.Should().BeEmpty();
        _ = car.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task ClickingANewAnchorSelectsItsNode()
    {
        var front = new ModelPublishedNode("Body/Door_Front_L", Guid.NewGuid());
        _ = LateRecordWriter.WriteRecord(this.documents, Car, change: record => record with { PublishedNodes = [front], NewNodes = [front] });
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);

        editor.Settings.Continuity!.New.Single().SelectCommand.Execute(parameter: null);

        var node = this.services.Scenes.ReferenceOf(car, NodeId.FromValue(front.NodeId))!;
        _ = this.services.Selection.GetContext(car.Id).SelectedNodes.Should().Equal(node);
    }

    [TestMethod]
    public async Task SelectingAMeshShowsItsDefaultsAndAnEditWritesThemByAnchor()
    {
        var (car, editor, body) = await this.OpenWithBodyAsync().ConfigureAwait(false);

        this.services.Selection.Publish(car.Id, SceneSelectionContext.ForNodes([body]), "Test");

        var mesh = editor.Settings.SelectedMesh!;
        _ = mesh.Title.Should().Be("Selected mesh: Body");
        _ = mesh.LevelOfDetail.IsAvailable.Should().BeTrue();
        mesh.Surfaces.Single().CastsShadows = false;
        mesh.LevelOfDetail.Kind = LodPolicyShapes.FixedKind;
        var settings = ModelOf(car).Record!.Settings;
        _ = ModelGeometryDefaults.SurfaceFlagOf(settings, "meshes/Body", PaintSurface, ModelSurfaceFlag.CastsShadows).Should().BeFalse();
        _ = ModelGeometryDefaults.LodPolicyOf(settings, "meshes/Body").Should().Be(new LodPolicy.Fixed(0));
        _ = car.History.UndoStack.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task SelectingAMeshOffersItsMaterialsTexturesAndAnOverrideKeepsTheModelsValue()
    {
        _ = this.services.MaterialTextures
            .Setup(value => value.ReadTexturesAsync(It.IsAny<ProjectContext>(), Car, It.IsAny<CancellationToken>()))
            .ReturnsAsync(ImmutableDictionary<Guid, ImmutableArray<string>>.Empty.Add(PaintMaterial, ["/Content/Car.glb/Paint_Normal.otex"]));
        var (car, editor, body) = await this.OpenWithBodyAsync().ConfigureAwait(false);

        this.services.Selection.Publish(car.Id, SceneSelectionContext.ForNodes([body]), "Test");
        var texture = editor.Settings.SelectedTexture;
        texture.FlipNormalGreen.Value = true;

        _ = texture.IsVisible.Should().BeTrue();
        _ = texture.Textures.Should().Equal("Paint_Normal");
        var settings = ModelOf(car).Record!.Settings;
        _ = ModelImportSettingScope.Texture("Paint_Normal").Read(ModelImportSettingCatalog.FlipNormalGreen, settings).Should().Be(true);
        _ = ModelImportSettingCatalog.FlipNormalGreen.ReadFrom(settings).Should().BeNull("the model's own value is unchanged");
        _ = texture.FlipNormalGreen.OverrideState.Should().Be(OverrideState.Full);
    }

    [TestMethod]
    public void TheLayoutKeepsAUsableWidth()
    {
        var layout = new ModelEditorLayout(settings: null);

        layout.PanelWidth = 10;
        layout.IsPanelCollapsed = true;

        _ = layout.PanelWidth.Should().Be(ModelEditorLayout.MinimumPanelWidth);
        _ = layout.IsPanelCollapsed.Should().BeTrue();
    }

    public void Dispose()
    {
        this.services.Dispose();
        this.documents.Dispose();
    }

    private static ModelContent ModelOf(Document document) => (ModelContent)document.Content;

    private static ContentCookResult Succeeded() => new(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);

    private static DiagnosticRecord Warning(string message) => new()
    {
        OperationId = Guid.NewGuid(),
        Domain = FailureDomain.ContentPipeline,
        Severity = DiagnosticSeverity.Warning,
        Code = "import.warning",
        Message = message,
    };

    // The surface provenance of one geometry, the body, as an import records it.
    private static JsonElement Provenance()
        => JsonSerializer.SerializeToElement(new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["schema_version"] = 2,
            ["geometries"] = new[]
            {
                new Dictionary<string, object>(StringComparer.Ordinal)
                {
                    ["geometry_asset_key"] = GeometryKey.ToString("D"),
                    ["source_geometry_anchor"] = "meshes/Body",
                    ["source_layout_witness"] = new string('a', 64),
                    ["layout_revision"] = new string('b', 64),
                    ["surfaces"] = Array.Empty<object>(),
                    ["allocations"] = Array.Empty<object>(),
                },
            },
        });

    private string RecordPathOf(ModelImportSettings record)
        => ModelImportSettings.PathFor(this.documents.Project.Root.FullPath, record.SourceIdentity);

    private async Task<(Document Car, ModelEditorViewModel Editor)> OpenAsync()
    {
        var car = await this.documents.OpenModelAsync(Car).ConfigureAwait(false);
        var tab = new DocumentTab(car, new ModelEditorProvider((ModelDocumentKind)car.Kind, this.services.Services).Editor);
        return (car, new ModelEditorViewModel(tab, this.services.Services));
    }

    // A model whose body node renders a geometry with one surface, two LODs and a material.
    private async Task<(Document Car, ModelEditorViewModel Editor, NodeReference Body)> OpenWithBodyAsync()
    {
        var bodyId = Guid.NewGuid();
        _ = LateRecordWriter.WriteRecord(this.documents, Car, change: record => record with
        {
            SurfaceProvenance = Provenance(),
            PublishedNodes = [new("Body", bodyId)],
        });
        var scene = new StructureScene(Guid.NewGuid(), Car.VirtualPath);
        var path = NodePath.Of(NodeId.FromValue(bodyId));
        this.services.Structures.Publish(new SceneStructure(Car, scene, [new StructureNode(path, "Body", Parent: null, scene, Instance: null, BodyGeometry, HasCamera: false)], []));
        GeometrySurfaceBinding[] bindings = [new(0, 0, PaintMaterial, TriangleCount: 12), new(1, 0, PaintMaterial, TriangleCount: 6)];
        _ = this.services.Surfaces
            .Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), BodyGeometry, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new GeometrySurfaceMetadata(BodyGeometry, GeometryKey, new string('c', 64), [new GeometrySurface(PaintSurface, "Paint", Visible: true, CastsShadows: true, [.. bindings])], new LodPolicy.Distance([5f], 0.1f)));
        var (car, editor) = await this.OpenAsync().ConfigureAwait(false);
        return (car, editor, this.services.Scenes.ReferenceOf(car, NodeId.FromValue(bodyId))!);
    }
}
