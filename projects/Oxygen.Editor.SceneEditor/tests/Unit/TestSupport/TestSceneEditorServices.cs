// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Unit.Tests.TestSupport;

/// <summary>
/// The session services of a scene editor without an engine: the selection, the hide and lock state and the stored
/// viewports are real and empty, the stores behind them are not, and the session has no play sessions.
/// </summary>
internal static class TestSceneEditorServices
{
    /// <summary>Creates the services of a scene editor in a session.</summary>
    /// <param name="session">The project session.</param>
    /// <param name="preview">Reports when the engine shows a scene; by default, never.</param>
    /// <param name="inputCommitter">Completes the focused control's input; by default, does nothing.</param>
    /// <param name="results">Receives the editor's operation results; by default, discards them.</param>
    /// <param name="previewSettings">The preview preferences; by default, a service over the session that stores nothing.</param>
    /// <returns>The services.</returns>
    public static SceneEditorServices Create(
        IProjectSession session,
        IScenePreview? preview = null,
        IDocumentInputCommitter? inputCommitter = null,
        IOperationResultPublisher? results = null,
        PreviewSettingsService? previewSettings = null)
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
            new SceneNodeInteraction(settings, publisher, reducer, NullLoggerFactory.Instance),
            new SceneEditorViews(),
            previewSettings ?? new PreviewSettingsService(engine, settings, session, publisher, reducer),
            new ViewportStateService(settings, publisher, reducer),
            new TransformToolSettingsService(settings: null),
            engineSettings.Object,
            NullLoggerFactory.Instance);
    }
}
