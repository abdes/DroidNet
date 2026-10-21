// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Input;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Workspace;

/// <summary>
///     The workspace window's own services: the panels and commands its features contribute, its documents and their
///     area, the project's content as the window sees it, the preview, Play, script tooling and the project-bound
///     preferences of its scenes.
/// </summary>
/// <remarks>
///     The window's scope registers this module after its router and docking, and before its features, whose services
///     use the window's. Its activation runs before theirs: the document service follows the window's tabs before
///     anything opens a document.
/// </remarks>
/// <param name="features">The features whose panels and commands the window shows.</param>
public sealed class WorkspaceWindowModule(IReadOnlyList<IFeatureModule> features) : IScopeModule<IWindowLayer>
{
    private static readonly Type[] Activated =
    [
        // The preview and the preview content demand follow the open scene documents.
        typeof(IScenePreview),
        typeof(PreviewContentDemand),
        typeof(PreviewSession),
        typeof(PlaySession),
    ];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <summary>
    ///     Gets the modules of a workspace window's scope after its router and docking: the window's own services, the
    ///     Shell's and the features'.
    /// </summary>
    /// <param name="features">The features whose panels and commands the window shows.</param>
    /// <returns>The modules, in registration and activation order.</returns>
    public static IReadOnlyList<IScopeModule<IWindowLayer>> Compose(IReadOnlyList<IFeatureModule> features)
    {
        ArgumentNullException.ThrowIfNull(features);
        return [new AssetsWindowModule(), new WorkspaceWindowModule(features), new ShellWindowModule(), new ContentPipelineWindowModule(), .. features];
    }

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The panels and commands of the window, and its documents and their kinds.
        RegisterContributions(scope, features);
        RegisterDocuments(scope);

        RegisterContentServices(scope);
        RegisterPreview(scope);
        scope.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);
        RegisterPreferences(scope);
        RegisterScriptServices(scope);
        RegisterPlayServices(scope);
    }

    // The panels the features contribute, which the registry checks once.
    private static void RegisterContributions(IRegistrator scope, IReadOnlyList<IFeatureModule> modules)
        => scope.RegisterDelegate(
            _ => new ContributionRegistry(
                [.. modules.SelectMany(static module => module.Panels)],
                [.. modules.SelectMany(static module => module.Commands)]),
            Reuse.Singleton);

    // Each file opens once, as a document of the kind registered for its suffix, shown by that kind's editor.
    // A workspace belongs to one project: activating another project closes every window first.
    private static void RegisterDocuments(IRegistrator scope)
    {
        scope.RegisterDelegate(
            resolver => new SceneDocumentKind(
                resolver.Resolve<IProjectContextService>().ActiveProject
                    ?? throw new InvalidOperationException("A workspace opens only with a project."),
                resolver.Resolve<ISceneNodeInteraction>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetRedirects>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => new OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>(), resolver.Resolve<MaterialDocumentKind>()],
                resolver.Resolve<IAtomicFileStore>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
    }

    // The project's content as the window sees it. The Content Explorer's scope reaches the same instances, created by
    // this window's scope whichever scope asks first.
    private static void RegisterContentServices(IRegistrator scope)
    {
        scope.Register<IAssetRuntimeStatusSource, AssetRuntimeStatusSource>(Reuse.Singleton);
    }

    // The engine preview and the preview content demand follow the open scene documents. The project's preview also
    // applies the content-mount changes the Content Explorer confirms.
    private static void RegisterPreview(IRegistrator scope)
    {
        scope.RegisterDelegate<IScenePreview>(
            resolver => ScenePreviews.Create(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IOperationResultPublisher>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetFeed>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => new PreviewContentDemand(ContentDemands.Start(
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IAssetFeed>(),
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<IProjectContextService>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault))),
            Reuse.Singleton);
        scope.Register<PreviewSession>(Reuse.Singleton);
        scope.RegisterMapping<IContentMountChanges, PreviewSession>();
    }

    // The preferences stored per project: preview settings, hidden and locked nodes, viewport layouts. They are the
    // window's until the project session owns them; the gizmo preferences, kept across projects, are the editor's.
    private static void RegisterPreferences(IRegistrator scope)
    {
        // A dedicated manager owns a separate DbContext; preview I/O is serialized by its service.
        scope.Register<IEditorSettingsManager, EditorSettingsManager>(Reuse.Singleton, serviceKey: nameof(PreviewSettingsService));
        scope.RegisterDelegate(
            resolver => new PreviewSettingsService(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService)),
                resolver.Resolve<IProjectContextService>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);

        // Editor hide / lock workspace state: one owner, project-scoped typed settings, serialized writes.
        scope.RegisterDelegate(
            resolver => new SceneNodeInteraction(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);

        // Scene commands obey the same hide / lock state through the Scenes interface.
        scope.RegisterMapping<ISceneNodeInteraction, SceneNodeInteraction>();

        // Scene viewport layout and pane cameras: user-local workspace state, serialized writes.
        scope.RegisterDelegate(
            resolver => new ViewportStateService(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
    }

    // Script and input authoring: the script and input facts, the code editor and project tooling. The family facts
    // follow this workspace's asset catalog, so they live as long as it does.
    private static void RegisterScriptServices(IRegistrator scope)
    {
        scope.RegisterInstance(ScriptToolingPaths.Discover());
        scope.Register<ProjectScriptTooling>(Reuse.Singleton);
        scope.RegisterMany([typeof(IScripts), typeof(IAssetProblemSource)], typeof(ScriptFacts), Reuse.Singleton);
        scope.RegisterMany([typeof(IInputSources), typeof(IAssetProblemSource)], typeof(InputFacts), Reuse.Singleton);
    }

    // Play sessions: the player runs the scenes of this project and ends with the workspace. Its problems and log reach
    // Problems and Output.
    private static void RegisterPlayServices(IRegistrator scope)
    {
        scope.Register<PlaySettings>(Reuse.Singleton);
        scope.Register<PlaySession>(Reuse.Singleton);
        scope.RegisterMapping<IRuntimeProblemSource, PlaySession>();
        scope.RegisterMapping<IAssetProblemSource, PlaySession>();
        scope.RegisterMapping<IPlayerLog, PlaySession>();
    }
}
