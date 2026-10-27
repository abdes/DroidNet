// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>The session's services that the model editors use.</summary>
/// <param name="viewports">Creates the editor's viewport and outlines its selection.</param>
/// <param name="viewportStates">Keeps each model's viewport camera and view options per user.</param>
/// <param name="views">The scene views, which reach each editor's viewport and the scene editor's placement.</param>
/// <param name="scenePreview">Reports when the engine shows a document's scene.</param>
/// <param name="scenes">The scene each open document shows, the model's preview scene included.</param>
/// <param name="selection">The selection of each document.</param>
/// <param name="openDocuments">The session's open documents, among them the scene a model is placed in.</param>
/// <param name="documents">Saves documents.</param>
/// <param name="contentPipeline">Reimports the model.</param>
/// <param name="session">The project session.</param>
/// <param name="assets">The project's assets, with each model's import state.</param>
/// <param name="structures">What each model contains, as last read.</param>
/// <param name="operationResults">Publishes what a reimport did.</param>
/// <param name="statusReducer">Reduces the published diagnostics into a severity.</param>
/// <param name="imports">Reimports a model and saves its import settings.</param>
/// <param name="problems">The kept import problems of the open models.</param>
/// <param name="layout">The import settings panel's width and collapse, per user.</param>
/// <param name="analyzer">Reads what a model's source holds, which fills the import settings.</param>
/// <param name="surfaces">Reads a geometry's surfaces and LOD policy, as the last import produced them.</param>
/// <param name="materialTextures">Reads which textures a model's materials use.</param>
/// <param name="cookRuns">The session's cook runs, after which a model's record and problems are read again.</param>
/// <param name="loggerFactory">The logger factory.</param>
public sealed class ModelEditorServices(
    ViewportServices viewports,
    ViewportStateService viewportStates,
    SceneViews views,
    IScenePreview scenePreview,
    DocumentScenes scenes,
    ISceneSelectionService selection,
    OpenDocuments openDocuments,
    IDocumentService documents,
    IContentPipelineService contentPipeline,
    IProjectSession session,
    IAssetFeed assets,
    ISceneStructures structures,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    ModelImports imports,
    ModelImportProblems problems,
    ModelEditorLayout layout,
    IModelSourceAnalyzer analyzer,
    IGeometrySurfaceProvider surfaces,
    IMaterialTextures materialTextures,
    ICookRunService cookRuns,
    ILoggerFactory loggerFactory)
{
    /// <summary>Gets what creates the editor's viewport and outlines its selection.</summary>
    public ViewportServices Viewports { get; } = viewports;

    /// <summary>Gets what keeps each model's viewport camera and view options per user.</summary>
    public ViewportStateService ViewportStates { get; } = viewportStates;

    /// <summary>Gets the scene views, which reach each editor's viewport and the scene editor's placement.</summary>
    public SceneViews Views { get; } = views;

    /// <summary>Gets the service that reports when the engine shows a document's scene.</summary>
    public IScenePreview ScenePreview { get; } = scenePreview;

    /// <summary>Gets the scene each open document shows.</summary>
    public DocumentScenes Scenes { get; } = scenes;

    /// <summary>Gets the selection of each document.</summary>
    public ISceneSelectionService Selection { get; } = selection;

    /// <summary>Gets the session's open documents.</summary>
    public OpenDocuments OpenDocuments { get; } = openDocuments;

    /// <summary>Gets the service that saves documents.</summary>
    public IDocumentService Documents { get; } = documents;

    /// <summary>Gets the content pipeline, which reimports the model.</summary>
    public IContentPipelineService ContentPipeline { get; } = contentPipeline;

    /// <summary>Gets the project session.</summary>
    public IProjectSession Session { get; } = session;

    /// <summary>Gets the project's assets, with each model's import state.</summary>
    public IAssetFeed Assets { get; } = assets;

    /// <summary>Gets what each model contains, as last read.</summary>
    public ISceneStructures Structures { get; } = structures;

    /// <summary>Gets the operation-result publisher.</summary>
    public IOperationResultPublisher OperationResults { get; } = operationResults;

    /// <summary>Gets the status reducer.</summary>
    public IStatusReducer StatusReducer { get; } = statusReducer;

    /// <summary>Gets what reimports a model and saves its import settings.</summary>
    public ModelImports Imports { get; } = imports;

    /// <summary>Gets the kept import problems of the open models.</summary>
    public ModelImportProblems Problems { get; } = problems;

    /// <summary>Gets the import settings panel's width and collapse, per user.</summary>
    public ModelEditorLayout Layout { get; } = layout;

    /// <summary>Gets what reads a model's source, which fills the import settings.</summary>
    public IModelSourceAnalyzer Analyzer { get; } = analyzer;

    /// <summary>Gets what reads a geometry's surfaces and LOD policy.</summary>
    public IGeometrySurfaceProvider Surfaces { get; } = surfaces;

    /// <summary>Gets what reads which textures a model's materials use.</summary>
    public IMaterialTextures MaterialTextures { get; } = materialTextures;

    /// <summary>Gets the session's cook runs.</summary>
    public ICookRunService CookRuns { get; } = cookRuns;

    /// <summary>Gets the logger factory.</summary>
    public ILoggerFactory LoggerFactory { get; } = loggerFactory;
}
