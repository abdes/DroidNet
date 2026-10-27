// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using DroidNet.Config;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.TestSupport;

/// <summary>The session's services a model editor uses, over test documents and an engine that shows nothing.</summary>
internal sealed class TestModelEditorServices : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="TestModelEditorServices"/> class.</summary>
    /// <param name="documents">The open documents: the model and the scenes beside it.</param>
    public TestModelEditorServices(TestSceneDocuments documents)
    {
        ArgumentNullException.ThrowIfNull(documents);
        var engine = Mock.Of<IEngineService>();
        var publisher = Mock.Of<IOperationResultPublisher>();
        var reducer = new OperationStatusReducer();
        var engineSettings = new Mock<ISettingsService<IEngineStartupSettings>>();
        _ = engineSettings.SetupGet(value => value.Settings).Returns(new EngineStartupSettings());
        this.PreviewSettings = new PreviewSettingsService(engine, Mock.Of<IEditorSettingsManager>(), documents.Session, publisher, reducer);
        this.Scenes = new DocumentScenes(documents.Open);
        _ = this.AssetFeed.SetupGet(value => value.Items).Returns(this.Assets);
        _ = this.AssetFeed.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
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

    /// <summary>Gets the content pipeline, which reimports nothing.</summary>
    public Mock<IContentPipelineService> ContentPipeline { get; } = new();

    /// <summary>Gets the scene each open document shows.</summary>
    public DocumentScenes Scenes { get; }

    private Mock<IAssetFeed> AssetFeed { get; } = new();

    private PreviewSettingsService PreviewSettings { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Scenes.Dispose();
        this.PreviewSettings.Dispose();
        this.Assets.Dispose();
    }
}
