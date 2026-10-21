// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
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
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Workspace;

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
    private IDocumentService? documentService;
    private IDocumentArea? documentArea;
    private IPlacementService? placement;
    private IAssetCatalog? assetCatalog;
    private PreviewSession? previewSession;
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

        // The preview starts the engine and mounts the project's content; the start scene opens even when it fails, so
        // authoring still works and the workspace shows why the preview is unavailable.
        if (this.previewSession is { } session && this.projectContextService.ActiveProject is { } project)
        {
            _ = await session.StartAsync(project).ConfigureAwait(true);
        }

        await this.OpenInitialSceneAsync().ConfigureAwait(true);
    }

    /// <inheritdoc />
    protected override void OnSetupChildContainer(IContainer childContainer)
    {
        // The panels and commands of the window, its documents, their kinds and editors, its document area and its
        // document service, and the placement of documents and panels.
        RegisterContributions(childContainer, this.modules);

        // The features register before anything is resolved, since the window's services take what they contribute,
        // such as the document editors; their services use the window's, which are all registered when they resolve.
        foreach (var module in this.modules)
        {
            module.RegisterWindowServices(childContainer);
        }

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

        this.RegisterPreviewSession(childContainer);
        this.RegisterScriptServices(childContainer);

        // The cook listens to the catalog's changes from the moment the workspace opens, and the Content Explorer's
        // Cook on change toggle reaches the same instance. It reads the family facts, registered with the scripts.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>();

        // The features activate last: their activation may use any feature's services.
        foreach (var module in this.modules)
        {
            module.ActivateWindowServices(childContainer);
        }
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//se:se;right;w=350//props:props;bottom=se//content:content;bottom;h=340//log:log;with=content//cook:cook;with=content//problems:problems;with=content//inspect:inspect;with=content)");

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.UnobservePreviewSession();

            this.ShutdownPlayServices();

            _ = this.previewSession?.DisposeAsync();
            this.previewSession = null;

            this.documentService = null;
            this.documentArea = null;
            this.placement = null;
            this.assetCatalog = null;
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
            _ => new ContributionRegistry(
                [.. modules.SelectMany(static module => module.Panels)],
                [.. modules.SelectMany(static module => module.Commands)]),
            Reuse.Singleton);

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

        // The project's global history: the Content Explorer's child container records into this instance, and it ends
        // with the workspace.
        _ = childContainer.Resolve<Oxygen.Editor.Shell.Documents.IHistoryService>();

        // One instance for the workspace: open documents register as participants on it, and the Content Explorer's
        // child container must reach the same instance instead of creating its own on first use.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>();

        // Likewise one script authoring service: the asset feed follows the files it reports creating, so the Content
        // Browser's New Script must reach the instance the feed listens to.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Authoring.IAssetAuthoring>();
    }

    // The project's preview, which also applies the content-mount changes the Content Explorer confirms.
    private void RegisterPreviewSession(IContainer childContainer)
    {
        childContainer.Register<PreviewSession>(Reuse.Singleton);
        childContainer.RegisterMapping<IContentMountChanges, PreviewSession>();
        this.previewSession = childContainer.Resolve<PreviewSession>();
        this.ObservePreviewSession();
    }

    private async Task OpenInitialSceneAsync()
    {
        if (this.projectManager.CurrentProject is not { } project
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

        var scene = await SceneFiles.FindInitialAsync(catalog, this.projectContextService.ActiveProject ?? context).ConfigureAwait(true);
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
            if (await SceneFiles.FindByNameOrIdAsync(catalog, context, lastOpened).ConfigureAwait(true) is not { } migrated
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
