// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reactive.Subjects;
using DroidNet.Config;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.TestSupport;

/// <summary>The session's services a model editor uses, over test documents and an engine that shows nothing.</summary>
/// <remarks>
/// The content pipeline, the source analysis, the geometry and material reads and the publication's kept diagnostics
/// answer nothing until the test sets them up; the cook runs complete when the test raises them.
/// </remarks>
internal sealed class TestModelEditorServices : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="TestModelEditorServices"/> class.</summary>
    /// <param name="documents">The open documents: the model and the scenes beside it.</param>
    /// <param name="records">The record writer the documents save models with, which this sets to its imports.</param>
    public TestModelEditorServices(TestSceneDocuments documents, LateRecordWriter? records = null)
    {
        ArgumentNullException.ThrowIfNull(documents);
        var engine = Mock.Of<IEngineService>();
        var publisher = this.Results.Object;
        var reducer = new OperationStatusReducer();
        var engineSettings = new Mock<ISettingsService<IEngineStartupSettings>>();
        _ = engineSettings.SetupGet(value => value.Settings).Returns(new EngineStartupSettings());
        this.PreviewSettings = new PreviewSettingsService(engine, Mock.Of<IEditorSettingsManager>(), documents.Session, publisher, reducer);
        this.Scenes = new DocumentScenes(documents.Open);
        _ = this.AssetFeed.SetupGet(value => value.Items).Returns(this.Assets);
        _ = this.AssetFeed.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = this.Published
            .Setup(value => value.ReadDiagnosticsAsync(It.IsAny<ProjectContext>(), It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(ImmutableArray<DiagnosticRecord>.Empty);
        _ = this.MaterialTextures
            .Setup(value => value.ReadTexturesAsync(It.IsAny<ProjectContext>(), It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(ImmutableDictionary<Guid, ImmutableArray<string>>.Empty);
        this.Imports = new ModelImports(this.ContentPipeline.Object, documents.Session, publisher, reducer, NullLoggerFactory.Instance);
        this.Problems = new ModelImportProblems(documents.Open, this.Scenes, this.CookRuns.Object, this.Published.Object, documents.Session);
        records?.Target = this.Imports;
        this.Services = new ModelEditorServices(
            new ViewportServices(engine, this.PreviewSettings, engineSettings.Object, publisher, reducer),
            new ViewportStateService(documents.Session, Mock.Of<IEditorSettingsManager>(), publisher, reducer),
            this.Views,
            this.Preview.Object,
            this.Scenes,
            this.Selection,
            documents.Open,
            Mock.Of<IDocumentService>(),
            this.ContentPipeline.Object,
            documents.Session,
            this.AssetFeed.Object,
            this.Structures,
            publisher,
            reducer,
            this.Imports,
            this.Problems,
            new ModelEditorLayout(settings: null),
            this.Analyzer.Object,
            this.Surfaces.Object,
            this.MaterialTextures.Object,
            this.CookRuns.Object,
            NullLoggerFactory.Instance);
    }

    /// <summary>Gets the services.</summary>
    public ModelEditorServices Services { get; }

    /// <summary>Gets the editors that place instances in the scenes, which a test attaches its scene's view to.</summary>
    public SceneViews Views { get; } = new();

    /// <summary>Gets the selection of every document.</summary>
    public SceneSelectionService Selection { get; } = new();

    /// <summary>Gets what an instance of a source contains, as the test publishes it.</summary>
    public FakeSceneStructures Structures { get; } = new();

    /// <summary>Gets the project's assets, which the test publishes.</summary>
    public BehaviorSubject<IReadOnlyList<AssetEntry>> Assets { get; } = new([]);

    /// <summary>Gets the engine preview, which shows no document until the test says it does.</summary>
    public Mock<IScenePreview> Preview { get; } = new();

    /// <summary>Gets the content pipeline, which imports nothing until the test says what it does.</summary>
    public Mock<IContentPipelineService> ContentPipeline { get; } = new();

    /// <summary>Gets the source analysis, which reads nothing until the test says what a file holds.</summary>
    public Mock<IModelSourceAnalyzer> Analyzer { get; } = new();

    /// <summary>Gets the geometry reads, which find nothing until the test says what a geometry holds.</summary>
    public Mock<IGeometrySurfaceProvider> Surfaces { get; } = new();

    /// <summary>Gets the material reads, which find no texture until the test says which a material uses.</summary>
    public Mock<IMaterialTextures> MaterialTextures { get; } = new();

    /// <summary>Gets the diagnostics kept with each model's publication, none until the test says which.</summary>
    public Mock<IPublishedDiagnostics> Published { get; } = new();

    /// <summary>Gets the session's cook runs, which complete when the test raises them.</summary>
    public Mock<ICookRunService> CookRuns { get; } = new();

    /// <summary>Gets the operation results the editor publishes.</summary>
    public Mock<IOperationResultPublisher> Results { get; } = new();

    /// <summary>Gets what reimports models and saves their import settings.</summary>
    public ModelImports Imports { get; }

    /// <summary>Gets the kept import problems of the open models.</summary>
    public ModelImportProblems Problems { get; }

    /// <summary>Gets the scene each open document shows.</summary>
    public DocumentScenes Scenes { get; }

    private Mock<IAssetFeed> AssetFeed { get; } = new();

    private PreviewSettingsService PreviewSettings { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Problems.Dispose();
        this.Scenes.Dispose();
        this.PreviewSettings.Dispose();
        this.Assets.Dispose();
    }
}
