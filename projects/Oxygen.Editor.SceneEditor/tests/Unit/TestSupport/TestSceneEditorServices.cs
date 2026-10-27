// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Unit.Tests.TestSupport;

/// <summary>
/// The session services of a scene editor without an engine: the selection, the hide and lock state and the stored
/// viewports are real and empty, the stores behind them are not, and the session has no play sessions.
/// </summary>
internal static class TestSceneEditorServices
{
    /// <summary>Creates preview preferences over a session that stores nothing.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="results">Receives their operation results; by default, discards them.</param>
    /// <returns>The preferences, which the caller owns and disposes.</returns>
    public static PreviewSettingsService CreatePreviewSettings(IProjectSession session, IOperationResultPublisher? results = null)
        => new(
            Mock.Of<IEngineService>(),
            Mock.Of<IEditorSettingsManager>(),
            session,
            results ?? Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer());

    /// <summary>Creates the services of a scene editor in a session.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="previewSettings">The preview preferences, which the caller owns and disposes.</param>
    /// <param name="preview">Reports when the engine shows a scene; by default, never.</param>
    /// <param name="inputCommitter">Completes the focused control's input; by default, does nothing.</param>
    /// <param name="results">Receives the editor's operation results; by default, discards them.</param>
    /// <param name="assets">The project's assets; by default, none.</param>
    /// <param name="references">The saved authored references; by default, none.</param>
    /// <returns>The services.</returns>
    public static SceneEditorServices Create(
        IProjectSession session,
        PreviewSettingsService previewSettings,
        IScenePreview? preview = null,
        IDocumentInputCommitter? inputCommitter = null,
        IOperationResultPublisher? results = null,
        IAssetFeed? assets = null,
        IAuthoredReferences? references = null)
    {
        var engine = Mock.Of<IEngineService>();
        var publisher = results ?? Mock.Of<IOperationResultPublisher>();
        var reducer = new OperationStatusReducer();
        var settings = Mock.Of<IEditorSettingsManager>();
        var engineSettings = new Mock<ISettingsService<IEngineStartupSettings>>();
        _ = engineSettings.SetupGet(value => value.Settings).Returns(new EngineStartupSettings());
        return new SceneEditorServices(
            engine,
            preview ?? Mock.Of<IScenePreview>(),
            inputCommitter ?? Mock.Of<IDocumentInputCommitter>(),
            publisher,
            reducer,
            Mock.Of<IContentPipelineService>(),
            session,
            Mock.Of<IDocumentService>(),
            Mock.Of<IUnsavedCookInputs>(),
            new SceneSelectionService(),
            new SceneInteractionKit().Create(session, publisher, reducer),
            new SceneViews(),
            new ViewportServices(engine, previewSettings, engineSettings.Object, publisher, reducer),
            previewSettings,
            new ViewportStateService(session, settings, publisher, reducer),
            new TransformToolSettingsService(settings: null),
            NullLoggerFactory.Instance,
            play: null,
            assets,
            references);
    }
}
