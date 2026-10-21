// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
///     The ViewModel of the world editor docking workspace.
/// </summary>
/// <remarks>
///     The world editor uses a child IoC container and a child router. This gurantees that routes and
///     resolutions are isolated from the rest of the application, and that the workspace can be easily
///     replaced or extended by other modules. In the other hand, this also requires that resolutions
///     inside the workspace must always use the child container, and that navigations, even the absolute
///     ones, will always be relative to the workspace.
/// </remarks>
public partial class WorkspaceViewModel : DockingWorkspaceViewModel
{
    private readonly IContainer container;
    private readonly IReadOnlyList<IFeatureModule> modules;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectManagerService projectManager;
    private readonly IProjectUsageService projectUsage;
    private readonly IEngineService engineService;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly ILogger logger;
    private SceneDocumentLifetime? sceneLifetime;
    private IDocumentService? documentService;
    private IDocumentArea? documentArea;
    private IPlacementService? placement;
    private IAssetCatalog? assetCatalog;
    private PreviewSettingsService? previewSettings;
    private Oxygen.Editor.World.Workspace.EditingViewMaskService? editingViewMask;
    private PreviewSession? previewSession;
    private IMessenger? messenger;
    private IProject? initialSceneProject;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="container">The IoC container for dependency resolution.</param>
    /// <param name="router">The router for navigation within the workspace.</param>
    /// <param name="projectContextService">The active project context service.</param>
    /// <param name="projectManager">The project authoring service.</param>
    /// <param name="projectUsage">The per-project usage store (last-opened scene).</param>
    /// <param name="engineService">The engine service for mounting cooked roots.</param>
    /// <param name="operationResults">The visible operation-result publisher.</param>
    /// <param name="statusReducer">The diagnostic status reducer.</param>
    /// <param name="modules">The features whose panels and services each workspace window has.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "will generate another warning due to capture of container arg")]
    public WorkspaceViewModel(
        IContainer container,
        IRouter router,
        IProjectContextService projectContextService,
        IProjectManagerService projectManager,
        IProjectUsageService projectUsage,
        IEngineService engineService,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        IEnumerable<IFeatureModule> modules,
        ILoggerFactory? loggerFactory = null)
        : base(container, router, loggerFactory)
    {
        ArgumentNullException.ThrowIfNull(modules);
        this.container = container;
        this.modules = [.. modules];
        this.RoutesConfig = CreateRoutes(this.modules);
        this.projectContextService = projectContextService;
        this.projectManager = projectManager;
        this.projectUsage = projectUsage;
        this.engineService = engineService;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.logger = loggerFactory?.CreateLogger<WorkspaceViewModel>() ?? Microsoft.Extensions.Logging.Abstractions.NullLogger<WorkspaceViewModel>.Instance;
    }

    /// <inheritdoc />
    protected override object ThisViewModel => this;

    /// <inheritdoc />
    protected override string CenterOutletName => "renderer";

    /// <inheritdoc />
    protected override IRoutes RoutesConfig { get; }

    /// <inheritdoc />
    public override async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        await base.OnNavigatedToAsync(route, navigationContext).ConfigureAwait(true);
        this.OnScriptingNavigated();

        // Ensure messenger is available (resolve from container as fallback)
        this.messenger ??= this.container.Resolve<IMessenger>();
        this.messenger?.RegisterAll(this);

        // The preview starts the engine and mounts the project's content; the start scene opens even when it fails, so
        // authoring still works and the workspace shows why the preview is unavailable.
        if (this.previewSession is { } session && this.projectContextService.ActiveProject is { } project)
        {
            _ = await session.StartAsync(project).ConfigureAwait(true);
        }

        await this.OpenInitialSceneAsync().ConfigureAwait(true);
    }

    /// <summary>Resolves a scene from a last-opened name by file stem, file name, or stable ID.</summary>
    /// <param name="catalog">The project's asset catalog, which lists its scene files.</param>
    /// <param name="project">The project whose scene files are searched.</param>
    /// <param name="lastOpenedScene">The persisted last-opened scene name.</param>
    /// <returns>Where the matching scene is, or <see langword="null"/> when no scene matches.</returns>
    internal static async Task<ProjectAssetLocation?> ResolveSceneByNameOrIdAsync(IAssetCatalog catalog, ProjectContext project, string? lastOpenedScene)
    {
        if (string.IsNullOrWhiteSpace(lastOpenedScene))
        {
            return null;
        }

        if (Guid.TryParse(lastOpenedScene, out var id))
        {
            return await SceneFiles.FindByIdAsync(catalog, project, SceneId.FromValue(id)).ConfigureAwait(true);
        }

        var sceneName = lastOpenedScene.EndsWith(Oxygen.Managed.Assets.AssetFileTypes.SceneDescriptor, StringComparison.Ordinal)
            ? lastOpenedScene[..^Oxygen.Managed.Assets.AssetFileTypes.SceneDescriptor.Length]
            : lastOpenedScene;
        return await SceneFiles.FindByNameAsync(catalog, project, sceneName).ConfigureAwait(true);
    }

    /// <summary>Resolves the scene a workspace opens first: the one activation asked for, else the project's start scene.</summary>
    /// <param name="catalog">The project's asset catalog, which lists its scene files.</param>
    /// <param name="context">The project.</param>
    /// <returns>The scene's asset URI, or <see langword="null"/> when there is none to open.</returns>
    internal static async Task<AssetPath?> ResolveInitialSceneAsync(IAssetCatalog catalog, ProjectContext context)
    {
        // Fresh activation may carry an explicit scene request (template StarterScene or a workflow open).
        if (context.InitialScene is { } initialScene && context.FindSource(initialScene) is { IsMountRoot: false } requested)
        {
            return requested.Identity;
        }

        // Otherwise the project's configured start scene, resolved by stable ID.
        // A missing or invalid start scene is not silently replaced by last-opened or first-listed.
        return context.DefaultSceneId is { } defaultSceneId
            ? (await SceneFiles.FindByIdAsync(catalog, context, defaultSceneId).ConfigureAwait(true))?.Identity
            : null;
    }

    /// <summary>Runs an editor-wide scene shortcut in the active scene editor or Scene Explorer.</summary>
    /// <param name="shortcut">The shortcut.</param>
    /// <returns><see langword="true"/> when a receiver ran it.</returns>
    internal bool RunSceneShortcut(SceneShortcut shortcut)
    {
        if (this.messenger is not { } messenger)
        {
            return false;
        }

        var request = messenger.Send(new SceneShortcutMessage(shortcut));
        return request.HasReceivedResponse && request.Response;
    }

    /// <inheritdoc />
    protected override void OnSetupChildContainer(IContainer childContainer)
    {
        this.previewSettings = childContainer.Resolve<PreviewSettingsService>();

        // Per-window on purpose: it observes this window's workspace hidden set and unsubscribes when
        // the window closes, so a second window cannot inherit a stale "pending suppression" state.
        this.editingViewMask = new Oxygen.Editor.World.Workspace.EditingViewMaskService(
            childContainer.Resolve<Oxygen.Editor.World.Workspace.WorkspaceInteractionService>(),
            childContainer.Resolve<IOperationResultPublisher>(),
            childContainer.Resolve<IStatusReducer>()).Start();
        childContainer.Register<IMessenger, StrongReferenceMessenger>(Reuse.Singleton);

        // Resolve messenger instance from the child container so the view model can use it.
        this.messenger = childContainer.Resolve<IMessenger>();
        this.messenger.Register<ShowAssetRequestMessage>(this, (_, message) => message.Reply(this.ShowInspectionAssetAsync(message)));
        this.messenger.Register<OpenCookedInspectionRequestMessage>(this, (_, message) => message.Reply(this.OpenInspectionAsync(message)));
        this.messenger.Register<ChangeContentMountsRequestMessage>(this, (_, message) => message.Reply(this.ChangeContentMountsAsync(message)));

        // The panels and commands of the window, its documents, their kinds and editors, its document area and its
        // document service, and the placement of documents and panels.
        RegisterContributions(childContainer, this.modules);
        RegisterDocuments(childContainer);
        childContainer.Register<ICommandService, CommandService>(Reuse.Singleton);
        childContainer.Register<IPlacementService, DockingPlacementService>(Reuse.Singleton);

        RegisterContentServices(childContainer);
        this.assetCatalog = childContainer.Resolve<IAssetCatalog>();

        // The engine preview and the preview content demand follow the open scene documents.
        childContainer.RegisterDelegate<IScenePreview>(
            resolver => ScenePreviews.Create(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IOperationResultPublisher>(IfUnresolved.ReturnDefault),
                resolver.Resolve<IAssetFeed>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new PreviewContentDemand(ContentDemands.Start(
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IAssetFeed>(),
                resolver.Resolve<Oxygen.Editor.ContentPipeline.IContentPipelineService>(),
                resolver.Resolve<IProjectContextService>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault))),
            Reuse.Singleton);
        childContainer.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);

        // Everything the documents need is registered: the preview and the content demand follow them from now on.
        _ = childContainer.Resolve<IScenePreview>();
        _ = childContainer.Resolve<PreviewContentDemand>();

        // The document service follows the window's tabs before anything opens a document, and the window's close asks
        // about its documents.
        this.documentService = childContainer.Resolve<IDocumentService>();
        this.documentArea = childContainer.Resolve<IDocumentArea>();
        this.placement = childContainer.Resolve<IPlacementService>();
        _ = childContainer.Resolve<DocumentAreaViewModel>();
        _ = childContainer.Resolve<WindowCloseCoordinator>();
        childContainer.Register<SceneDocumentLifetime>(Reuse.Singleton);
        this.sceneLifetime = childContainer.Resolve<SceneDocumentLifetime>();

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        this.RegisterOutputPanels(childContainer);
        this.RegisterScriptServices(childContainer);

        // The cook listens to the catalog's changes from the moment the workspace opens, and the Content Browser's
        // Cook on change toggle reaches the same instance. It reads the family facts, registered with the scripts.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>();

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<SceneEditorView>(Reuse.Transient);
        childContainer.Register<MaterialEditorView>(Reuse.Transient);
        RegisterInspectorEditors(childContainer);
        this.RegisterAssetReveal(childContainer);

        // The features come last: their services use the window's, and their activation any feature's.
        foreach (var module in this.modules)
        {
            module.RegisterWindowServices(childContainer);
        }

        foreach (var module in this.modules)
        {
            module.ActivateWindowServices(childContainer);
        }

        this.cookedInspections = childContainer.Resolve<ICookedInspections>();
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//se:se;right;w=350//props:props;bottom=se//cb:cb;bottom;h=340//log:log;with=cb//cook:cook;with=cb//problems:problems;with=cb//inspect:inspect;with=cb)");

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            if (this.projectContextService.ActiveProject is { } project)
            {
                this.previewSettings?.Deactivate(project);
            }

            this.UnobservePreviewSession();

            this.editingViewMask?.Dispose();
            this.editingViewMask = null;
            this.ShutdownPlayServices();

            _ = this.previewSession?.DisposeAsync();
            this.previewSession = null;

            this.sceneLifetime?.Dispose();
            this.sceneLifetime = null;
            this.documentService = null;
            this.documentArea = null;
            this.placement = null;
            this.assetCatalog = null;
            this.messenger?.UnregisterAll(this);
        }

        base.Dispose(disposing);
    }

    // The window's own routes, and one per contributed panel, shown in the outlet of the panel's identity.
    private static Routes CreateRoutes(IEnumerable<IFeatureModule> modules)
        => new(
        [
            new Route
            {
                Path = string.Empty,
                Children = new Routes(
                [
                    new Route { Outlet = "renderer", Path = "dx", ViewModelType = typeof(DocumentAreaViewModel) },
                    new Route
                    {
                        Outlet = "se",
                        Path = "se",
                        MatchMethod = PathMatch.Full,
                        ViewModelType = typeof(SceneExplorerViewModel),
                    },
                    new Route
                    {
                        Outlet = "cb",
                        Path = "cb",
                        MatchMethod = PathMatch.Full,
                        ViewModelType = typeof(ContentBrowserViewModel),
                    },
                    new Route
                    {
                        Outlet = "props",
                        Path = "props",
                        MatchMethod = PathMatch.Full,
                        ViewModelType = typeof(SceneNodeEditorViewModel),
                    },
                    .. modules.SelectMany(static module => module.Panels).Select(static panel => new Route
                    {
                        Outlet = panel.Id, Path = panel.Id, MatchMethod = PathMatch.Full, ViewModelType = panel.ViewModelType,
                    }),
                ]),
            },
        ]);

    // The panels the features contribute, which the registry checks once.
    private static void RegisterContributions(IContainer childContainer, IEnumerable<IFeatureModule> modules)
        => childContainer.RegisterDelegate(
            _ => new ContributionRegistry([.. modules.SelectMany(static module => module.Panels)], []),
            Reuse.Singleton);

    private static void RegisterInspectorEditors(IContainer childContainer)
    {
        childContainer.Register<TransformViewModel>(Reuse.Transient);
        childContainer.Register<TransformView>(Reuse.Transient);
        childContainer.Register<PerspectiveCameraViewModel>(Reuse.Transient);
        childContainer.Register<PerspectiveCameraView>(Reuse.Transient);
        childContainer.Register<DirectionalLightViewModel>(Reuse.Transient);
        childContainer.Register<DirectionalLightView>(Reuse.Transient);
        childContainer.Register<OrthographicCameraViewModel>(Reuse.Transient);
        childContainer.Register<OrthographicCameraView>(Reuse.Transient);
        childContainer.Register<PointLightViewModel>(Reuse.Transient);
        childContainer.Register<PointLightView>(Reuse.Transient);
        childContainer.Register<SpotLightViewModel>(Reuse.Transient);
        childContainer.Register<SpotLightView>(Reuse.Transient);
        childContainer.Register<LocalFogVolumeViewModel>(Reuse.Transient);
        childContainer.Register<LocalFogVolumeView>(Reuse.Transient);
        childContainer.Register<Inspector.Scripting.ScriptingViewModel>(Reuse.Transient);
        childContainer.Register<Inspector.Scripting.ScriptingView>(Reuse.Transient);
        childContainer.Register<NodeRenderingViewModel>(Reuse.Transient);
        childContainer.Register<NodeRenderingView>(Reuse.Transient);
        childContainer.Register<EnvironmentViewModel>(Reuse.Transient);
        childContainer.Register<EnvironmentView>(Reuse.Transient);
        childContainer.Register<GeometryViewModel>(Reuse.Transient);
        childContainer.Register<GeometryView>(Reuse.Transient);
    }

    private static void RegisterContentServices(IContainer childContainer)
    {
        childContainer.Register<AssetCatalog>(Reuse.Singleton);
        childContainer.RegisterMapping<IAssetCatalog, AssetCatalog>();
        childContainer.RegisterMapping<IAssetFolderSource, AssetCatalog>();
        childContainer.RegisterMapping<IAssetPublicationTarget, AssetCatalog>();
        childContainer.RegisterDelegate<Oxygen.Editor.ContentPipeline.Status.IAssetCookStatusReader>(
            resolver => resolver.Resolve<Oxygen.Editor.ContentPipeline.IContentPipelineService>(), Reuse.Singleton);
        childContainer.Register<IAssetRuntimeStatusSource, AssetRuntimeStatusSource>(Reuse.Singleton);
        childContainer.Register<IAssetFeed, AssetFeed>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService, Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationService>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.ContentPipeline.Authoring.IAssetAuthoring, Oxygen.Editor.ContentPipeline.Authoring.AssetAuthoring>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService, Oxygen.Editor.ContentPipeline.Cooking.AutomaticCookService>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.ContentPipeline.Cooking.IUnsavedCookInputs, Oxygen.Editor.ContentPipeline.Cooking.UnsavedCookInputs>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.Shell.Documents.IHistoryService, Oxygen.Editor.Shell.Documents.HistoryService>(Reuse.Singleton);

        // The project's global history: the Content Browser's child container records into this instance, and it ends
        // with the workspace.
        _ = childContainer.Resolve<Oxygen.Editor.Shell.Documents.IHistoryService>();

        // One instance for the workspace: open documents register as participants on it, and the Content Browser's
        // child container must reach the same instance instead of creating its own on first use.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>();

        // Likewise one script authoring service: the asset feed follows the files it reports creating, so the Content
        // Browser's New Script must reach the instance the feed listens to.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Authoring.IAssetAuthoring>();
    }

    private void RegisterOutputPanels(IContainer childContainer)
    {
        childContainer.Register<PreviewSession>(Reuse.Singleton);
        this.previewSession = childContainer.Resolve<PreviewSession>();
        this.ObservePreviewSession();

    }

    private async Task OpenInitialSceneAsync()
    {
        if (this.messenger is null
            || this.projectManager.CurrentProject is not { } project
            || this.projectContextService.ActiveProject is not { } context
            || this.assetCatalog is not { } catalog
            || !context.OpenInitialScene
            || ReferenceEquals(project, this.initialSceneProject))
        {
            return;
        }

        this.initialSceneProject = project;

        // Seed the default scene for projects that predate DefaultSceneId before resolving it.
        await this.MigrateDefaultSceneAsync(catalog, project, context).ConfigureAwait(true);

        var scene = await ResolveInitialSceneAsync(catalog, this.projectContextService.ActiveProject ?? context).ConfigureAwait(true);
        if (scene is null)
        {
            return;
        }

        if (this.documentService is null)
        {
            this.LogInitialSceneManagerUnavailable(scene);
            return;
        }

        var opened = await this.documentService.OpenAsync(scene).ConfigureAwait(true);
        if (!opened.Succeeded)
        {
            this.LogInitialSceneNotOpened(scene, context.Name);
        }
    }

    /// <summary>
    ///     Seeds the project's configured default scene from the last-opened scene when the project
    ///     predates <see cref="IProjectInfo.DefaultSceneId"/> and no default has been persisted yet.
    /// </summary>
    /// <remarks>
    ///     This is a one-time migration: once a default exists it is authoritative and last-opened
    ///     usage no longer influences startup. The default is project metadata persisted in
    ///     <c>Project.oxy</c>; it is not scene undo/dirty state.
    /// </remarks>
    private async Task MigrateDefaultSceneAsync(IAssetCatalog catalog, IProject project, ProjectContext context)
    {
        if (project.ProjectInfo.DefaultSceneId is not null)
        {
            return;
        }

        try
        {
            var usage = await this.projectUsage.GetProjectUsageAsync(context.Name, context.ProjectRoot.FullPath).ConfigureAwait(true);
            if (string.IsNullOrWhiteSpace(usage?.LastOpenedScene))
            {
                return;
            }

            var lastOpened = usage.LastOpenedScene;
            if (await ResolveSceneByNameOrIdAsync(catalog, context, lastOpened).ConfigureAwait(true) is not { } migrated
                || await SceneFiles.ReadIdAsync(context, migrated).ConfigureAwait(true) is not { } migratedId)
            {
                return;
            }

            project.ProjectInfo.DefaultSceneId = migratedId;
            _ = await this.projectManager.SaveProjectInfoAsync(project.ProjectInfo).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneRestorationFailed(ex, context.Name);
        }
    }
}
