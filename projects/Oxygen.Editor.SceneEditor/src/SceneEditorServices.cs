// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The project session's services that every scene editor of the session uses. The session builds them once; each
/// editor receives them with its document's tab.
/// </summary>
/// <param name="engine">Coordinates native engine usage for the documents.</param>
/// <param name="scenePreview">Reports when the engine shows a document's scene.</param>
/// <param name="inputCommitter">Completes the focused inspector control's text input.</param>
/// <param name="operationResults">The visible operation-result publisher.</param>
/// <param name="statusReducer">The diagnostic status reducer.</param>
/// <param name="contentPipeline">The explicit content cooking service.</param>
/// <param name="session">The project session, whose project the viewport layouts are stored for.</param>
/// <param name="documents">The session's documents, which Play and Cook save before they run.</param>
/// <param name="unsavedInputs">Finds the unsaved documents a cook reads.</param>
/// <param name="selection">The scene selection of each document.</param>
/// <param name="interaction">The hide and lock state of the scene nodes.</param>
/// <param name="views">The registry of the editors that frame and move cameras.</param>
/// <param name="previewSettings">The shared project preview preferences.</param>
/// <param name="viewportStates">The stored viewport layouts and pane cameras.</param>
/// <param name="transformTools">The gizmo preferences.</param>
/// <param name="engineSettings">The engine startup settings, whose logging verbosity the editors edit.</param>
/// <param name="loggerFactory">The logger factory.</param>
/// <param name="play">The session's play sessions; without them, the editors have no Play controls.</param>
/// <param name="assets">The project's assets; without them, Quick Add offers no model or scene to instance.</param>
/// <param name="references">The saved authored references; without them, only a scene instancing itself is refused.</param>
public sealed class SceneEditorServices(
    IEngineService engine,
    IScenePreview scenePreview,
    IDocumentInputCommitter inputCommitter,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    IContentPipelineService contentPipeline,
    IProjectSession session,
    IDocumentService documents,
    IUnsavedCookInputs unsavedInputs,
    ISceneSelectionService selection,
    SceneNodeInteraction interaction,
    SceneEditorViews views,
    PreviewSettingsService previewSettings,
    ViewportStateService viewportStates,
    TransformToolSettingsService transformTools,
    ISettingsService<IEngineStartupSettings> engineSettings,
    ILoggerFactory loggerFactory,
    PlaySession? play = null,
    IAssetFeed? assets = null,
    IAuthoredReferences? references = null)
{
    /// <summary>Gets the service that coordinates native engine usage for the documents.</summary>
    public IEngineService Engine { get; } = engine;

    /// <summary>Gets the service that reports when the engine shows a document's scene.</summary>
    public IScenePreview ScenePreview { get; } = scenePreview;

    /// <summary>Gets the service that completes the focused inspector control's text input.</summary>
    public IDocumentInputCommitter InputCommitter { get; } = inputCommitter;

    /// <summary>Gets the visible operation-result publisher.</summary>
    public IOperationResultPublisher OperationResults { get; } = operationResults;

    /// <summary>Gets the diagnostic status reducer.</summary>
    public IStatusReducer StatusReducer { get; } = statusReducer;

    /// <summary>Gets the explicit content cooking service.</summary>
    public IContentPipelineService ContentPipeline { get; } = contentPipeline;

    /// <summary>Gets the project session.</summary>
    public IProjectSession Session { get; } = session;

    /// <summary>Gets the session's documents.</summary>
    public IDocumentService Documents { get; } = documents;

    /// <summary>Gets the service that finds the unsaved documents a cook reads.</summary>
    public IUnsavedCookInputs UnsavedInputs { get; } = unsavedInputs;

    /// <summary>Gets the scene selection of each document.</summary>
    public ISceneSelectionService Selection { get; } = selection;

    /// <summary>Gets the hide and lock state of the scene nodes.</summary>
    public SceneNodeInteraction Interaction { get; } = interaction;

    /// <summary>Gets the registry of the editors that frame and move cameras.</summary>
    public SceneEditorViews Views { get; } = views;

    /// <summary>Gets the shared project preview preferences.</summary>
    public PreviewSettingsService PreviewSettings { get; } = previewSettings;

    /// <summary>Gets the stored viewport layouts and pane cameras.</summary>
    public ViewportStateService ViewportStates { get; } = viewportStates;

    /// <summary>Gets the gizmo preferences.</summary>
    public TransformToolSettingsService TransformTools { get; } = transformTools;

    /// <summary>Gets the engine startup settings.</summary>
    public ISettingsService<IEngineStartupSettings> EngineSettings { get; } = engineSettings;

    /// <summary>Gets the logger factory.</summary>
    public ILoggerFactory LoggerFactory { get; } = loggerFactory;

    /// <summary>Gets the session's play sessions, if the session has them.</summary>
    public PlaySession? Play { get; } = play;

    /// <summary>Gets the project's assets, which Quick Add offers as instance sources, if the session lists them.</summary>
    public IAssetFeed? Assets { get; } = assets;

    /// <summary>Gets the saved authored references that instance sources are checked against, if the session reads them.</summary>
    public IAuthoredReferences? References { get; } = references;
}
