// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Output;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
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
public partial class WorkspaceViewModel : DockingWorkspaceViewModel, ICookingWorkspaceActions
{
    private readonly IContainer container;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectManagerService projectManager;
    private readonly IProjectUsageService projectUsage;
    private readonly IEngineService engineService;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly ILogger logger;
    private readonly SemaphoreSlim engineStartupGate = new(initialCount: 1, maxCount: 1);
    private DocumentManager? documentManager;
    private PreviewSettingsService? previewSettings;
    private Oxygen.Editor.World.Workspace.EditingViewMaskService? editingViewMask;
    private ProjectContext? previewProject;
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
        ILoggerFactory? loggerFactory = null)
        : base(container, router, loggerFactory)
    {
        this.container = container;
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
    protected override IRoutes RoutesConfig { get; } = new Routes(
    [
        new Route
        {
            Path = string.Empty,
            Children = new Routes(
            [
                new Route { Outlet = "renderer", Path = "dx", ViewModelType = typeof(DocumentHostViewModel) },
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
                new Route
                {
                    Outlet = "log", Path = "log", MatchMethod = PathMatch.Full, ViewModelType = typeof(OutputViewModel),
                },
                new Route
                {
                    Outlet = "cook", Path = "cook", MatchMethod = PathMatch.Full, ViewModelType = typeof(CookingPanelViewModel),
                },
                new Route
                {
                    Outlet = ProblemsDockId, Path = ProblemsDockId, MatchMethod = PathMatch.Full, ViewModelType = typeof(Oxygen.Editor.World.Problems.ProblemsViewModel),
                },
            ]),
        },
    ]);

    /// <inheritdoc />
    public override async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        await base.OnNavigatedToAsync(route, navigationContext).ConfigureAwait(true);
        if (DroidNet.Docking.Dockable.FromId("cook") is { } cookingDock)
        {
            cookingDock.Title = "Cooking";
        }

        this.OnScriptingNavigated();

        // Ensure messenger is available (resolve from container as fallback)
        this.messenger ??= this.container.Resolve<IMessenger>();
        this.messenger?.RegisterAll(this);

        if (!await this.EnsureEngineRunningAsync().ConfigureAwait(true))
        {
            return;
        }

        // Mount the project's cooked assets root in the engine's virtual path resolver.
        // This allows the engine to resolve asset:/// URIs to actual files on disk.
        await this.RefreshCookedRootsAsync().ConfigureAwait(true);

        await this.OpenInitialSceneAsync().ConfigureAwait(true);
    }

    /// <summary>Resolves a scene from a last-opened name by file stem, file name, or stable ID.</summary>
    /// <param name="project">The project whose scene files are searched.</param>
    /// <param name="lastOpenedScene">The persisted last-opened scene name.</param>
    /// <returns>Where the matching scene is, or <see langword="null"/> when no scene matches.</returns>
    internal static async Task<ProjectAssetLocation?> ResolveSceneByNameOrIdAsync(ProjectContext project, string? lastOpenedScene)
    {
        if (string.IsNullOrWhiteSpace(lastOpenedScene))
        {
            return null;
        }

        if (Guid.TryParse(lastOpenedScene, out var id))
        {
            return await SceneFiles.FindByIdAsync(project, SceneId.FromValue(id)).ConfigureAwait(true);
        }

        var sceneName = lastOpenedScene.EndsWith(Oxygen.Managed.Assets.AssetFileTypes.SceneDescriptor, StringComparison.OrdinalIgnoreCase)
            ? lastOpenedScene[..^Oxygen.Managed.Assets.AssetFileTypes.SceneDescriptor.Length]
            : lastOpenedScene;
        return SceneFiles.FindByName(project, sceneName);
    }

    /// <summary>Resolves the scene a workspace opens first: the one activation asked for, else the project's start scene.</summary>
    /// <param name="context">The project.</param>
    /// <returns>The scene's asset URI, or <see langword="null"/> when there is none to open.</returns>
    internal static async Task<Uri?> ResolveInitialSceneAsync(ProjectContext context)
    {
        // Fresh activation may carry an explicit scene request (template StarterScene or a workflow open).
        if (context.InitialSceneAssetUri is { } initialSceneAssetUri
            && context.FindByAssetUri(initialSceneAssetUri) is { IsMountRoot: false } requested && File.Exists(requested.FilePath))
        {
            return requested.AssetUri;
        }

        // Otherwise the project's configured start scene, resolved by stable ID.
        // A missing or invalid start scene is not silently replaced by last-opened or first-listed.
        return context.DefaultSceneId is { } defaultSceneId
            ? (await SceneFiles.FindByIdAsync(context, defaultSceneId).ConfigureAwait(true))?.AssetUri
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
        this.messenger.Register<ChangeContentMountsRequestMessage>(this, (_, message) => message.Reply(this.ChangeContentMountsAsync(message)));
        this.messenger.Register<ShowCookingJobsRequestMessage>(this, (_, _) => this.OnCookingRevealRequested(this, EventArgs.Empty));

        // DocumentHostViewModel must be registered and resolved first to ensure it subscribes to
        // IDocumentService events before DocumentManager starts handling open requests.
        childContainer.Register<DocumentHostViewModel>(Reuse.Singleton);
        childContainer.Register<DocumentHostView>(Reuse.Singleton);

        // Resolve DocumentHostViewModel immediately to ensure it subscribes to document events
        _ = childContainer.Resolve<DocumentHostViewModel>();

        // DocumentManager must be a singleton and resolved immediately to ensure it is always listening for messages
        // even if the router hasn't navigated to any editor yet.
        RegisterContentServices(childContainer);

        // Register scene-engine synchronization service
        childContainer.Register<ISceneEngineSync, SceneEngineSync>(Reuse.Singleton);
        childContainer.Register<ISceneMutator, SceneMutator>(Reuse.Singleton);
        childContainer.Register<ISceneOrganizer, SceneOrganizer>(Reuse.Singleton);
        childContainer.Register<ISceneContentDemandService, SceneContentDemandService>(Reuse.Singleton);
        _ = childContainer.Resolve<ISceneContentDemandService>();
        childContainer.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);
        childContainer.Register<ISceneDocumentCommandService, SceneDocumentCommandService>(Reuse.Singleton);

        // The workspace's open documents: each scene file opens once, as a document of the scene kind.
        childContainer.Register<SceneDocumentKind>(Reuse.Singleton);
        childContainer.RegisterDelegate(
            resolver => new Oxygen.Editor.Documents.OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>()],
                resolver.Resolve<DroidNet.Storage.IAtomicFileStore>(),
                resolver.Resolve<Microsoft.Extensions.Logging.ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        childContainer.Register<DocumentManager>(Reuse.Singleton);
        this.documentManager = childContainer.Resolve<DocumentManager>();

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        this.RegisterOutputPanels(childContainer);
        this.RegisterScriptServices(childContainer);

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<SceneEditorView>(Reuse.Transient);
        childContainer.Register<MaterialEditorView>(Reuse.Transient);
        childContainer.Register<Oxygen.Editor.World.Inspection.CookedInspectionView>(Reuse.Transient);
        RegisterInspectorEditors(childContainer);
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//se:se;right;w=350//props:props;bottom=se//cb:cb;bottom;h=340//log:log;with=cb//cook:cook;with=cb//problems:problems;with=cb)");

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            if (this.previewProject is { } project)
            {
                this.previewSettings?.Deactivate(project);
            }

            this.editingViewMask?.Dispose();
            this.editingViewMask = null;
            this.DisposeScriptServices();
            this.ShutdownPlayServices();

            this.publicationRegistration?.Dispose();
            this.publicationRegistration = null;

            if (this.cookingPanel is not null)
            {
                this.cookingPanel.RevealRequested -= this.OnCookingRevealRequested;
                this.cookingPanel.Dispose();
                this.cookingPanel = null;
            }

            if (this.engineService.State == EngineServiceState.Running)
            {
                _ = this.ReleaseCookedRootsAsync();
            }

            this.documentManager?.Dispose();
            this.documentManager = null;
            this.messenger?.UnregisterAll(this);
            this.engineStartupGate.Dispose();
        }

        base.Dispose(disposing);
    }

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
        childContainer.RegisterDelegate<Oxygen.Editor.ContentPipeline.Discovery.IBuiltinCatalogDiscovery>(
            resolver => new Oxygen.Editor.ContentPipeline.Discovery.BuiltinCatalogDiscovery(
                resolver.Resolve<Oxygen.Editor.ContentPipeline.IBuiltinGeometryCatalogProvider>(),
                resolver.Resolve<DroidNet.Storage.IAtomicFileStore>(),
                resolver.Resolve<DroidNet.Config.IPathFinder>(),
                resolver.Resolve<Microsoft.Extensions.Logging.ILogger<Oxygen.Editor.ContentPipeline.Discovery.BuiltinCatalogDiscovery>>(),
                resolver.Resolve<Oxygen.Managed.Core.Compatibility.INativeCompatibilityService>(Oxygen.Managed.Core.Compatibility.EditorNativeCompatibilityService.CookingServiceKey)),
            Reuse.Singleton);
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

        // The cook listens to the catalog's changes from the moment the workspace opens, and the Content Browser's
        // Cook on change toggle reaches the same instance.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>();
    }

    private void RegisterOutputPanels(IContainer childContainer)
    {
        this.cookHosting = childContainer.Resolve<DroidNet.Hosting.WinUI.HostingContext>();
        this.cookedCatalog = childContainer.Resolve<IAssetPublicationTarget>();
        if (this.projectContextService.ActiveProject is { } project)
        {
            this.publicationRegistration = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Publication.CookPublicationService>()
                .RegisterPreview(project, () => this.CreatePublicationPreviewAsync(project));
        }

        childContainer.Register<OutputViewModel>(Reuse.Singleton);
        childContainer.Register<OutputView>(Reuse.Transient);
        childContainer.RegisterInstance<ICookingWorkspaceActions>(this);
        childContainer.Register<CookingPanelViewModel>(Reuse.Singleton);
        childContainer.Register<CookingPanelView>(Reuse.Transient);
        this.cookingPanel = childContainer.Resolve<CookingPanelViewModel>();
        this.cookingPanel.RevealRequested += this.OnCookingRevealRequested;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The workspace reports recovery and native mount failures while preserving authoring access and runtime reader ownership.")]
    private async Task RefreshCookedRootsAsync()
    {
        if (this.projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        var stage = "publication admission";
        var mounted = false;
        try
        {
            var mountService = this.container.Resolve<Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountService>();
            var publication = this.container.Resolve<Oxygen.Editor.ContentPipeline.Publication.CookPublicationService>();
            var (reader, mounts) = await this.AdmitCookedContentAsync(publication, mountService, project).ConfigureAwait(true);
            using var retained = reader;
            var bindings = reader.Roots.Zip(
                reader.RootPaths,
                static (root, path) => new RuntimeCookedRoot(path, root.Owner == Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project ? root.Name : null)).ToArray();
            stage = "native mount and preview resume";
            await this.engineService.RefreshProjectCookedRootsAsync(bindings, mounts).ConfigureAwait(true);
            mounted = true;
            stage = "asset catalog refresh";
            var catalog = this.cookedCatalog ?? throw new InvalidOperationException("The workspace asset catalog is not initialized.");
            await catalog.UsePublicationAsync(reader, CancellationToken.None).ConfigureAwait(true);
            this.HasContentFailure = false;
            this.LogMountedRoots(mounts.Roots);
            await this.MaintainPublicationAsync(publication, project).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogCookedRootRefreshFailed(exception, stage, project.ProjectRoot, mounted ? "mounted" : "unavailable");
            this.ContentFailureTitle = mounted ? "Content Browser unavailable" : "Preview unavailable";
            this.ContentFailureMessage = $"Failed during {stage}: {exception.Message.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()}";
            this.HasContentFailure = true;
            RuntimeOperationResults.PublishFailure(
                this.operationResults,
                this.statusReducer,
                RuntimeOperationKinds.CookedRootRefresh,
                FailureDomain.AssetMount,
                AssetMountDiagnosticCodes.RefreshFailed,
                this.ContentFailureTitle,
                this.ContentFailureMessage,
                this.CreateProjectScope(),
                project.ProjectRoot,
                exception,
                exception.ToString());
            if (!mounted)
            {
                await this.SuspendUnavailablePreviewAsync(project).ConfigureAwait(true);
            }
        }
    }

    /// <summary>Publishes from the current libraries and admits that selection, republishing once when a library is re-cooked in between.</summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The admitted reader transfers to the caller; every rejected reader is disposed here.")]
    private async Task<(Oxygen.Editor.ContentPipeline.Publication.CookPublicationReadLease reader, Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountSet mounts)> AdmitCookedContentAsync(
        Oxygen.Editor.ContentPipeline.Publication.CookPublicationService publication,
        Oxygen.Editor.ContentPipeline.Mounting.CookedContentMountService mountService,
        ProjectContext project)
    {
        var reader = await publication.AcquireForMountAsync(project, CancellationToken.None).ConfigureAwait(true);
        try
        {
            try
            {
                return (reader, await mountService.PrepareAsync(project, reader, CancellationToken.None).ConfigureAwait(true));
            }
            catch (Oxygen.Editor.ContentPipeline.Publication.CookedLibraryChangedException changed)
            {
                this.LogCookedLibraryChanged(changed, project.ProjectRoot);
                reader.Dispose();
                reader = await publication.AcquireForMountAsync(project, CancellationToken.None).ConfigureAwait(true);
                return (reader, await mountService.PrepareAsync(project, reader, CancellationToken.None).ConfigureAwait(true));
            }
        }
        catch
        {
            reader.Dispose();
            throw;
        }
    }

    private async Task MaintainPublicationAsync(Oxygen.Editor.ContentPipeline.Publication.CookPublicationService publication, ProjectContext project)
    {
        try
        {
            var cleanupFailures = await publication.MaintainAsync(project, CancellationToken.None).ConfigureAwait(true);
            if (cleanupFailures.Count != 0)
            {
                this.PublishCookedRootWarning(
                    "Cook.CleanupDeferred",
                    "Unused output cleanup deferred",
                    string.Join(Environment.NewLine, cleanupFailures),
                    project.ProjectRoot);
            }
        }
        catch (Exception cleanup) when (cleanup is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
        {
            this.PublishCookedRootWarning("Cook.CleanupDeferred", "Unused output cleanup deferred", cleanup.Message, project.ProjectRoot, cleanup);
        }
    }

    private async Task OpenInitialSceneAsync()
    {
        if (this.messenger is null
            || this.projectManager.CurrentProject is not { } project
            || this.projectContextService.ActiveProject is not { } context
            || !context.OpenInitialScene
            || ReferenceEquals(project, this.initialSceneProject))
        {
            return;
        }

        this.initialSceneProject = project;

        // Seed the default scene for projects that predate DefaultSceneId before resolving it.
        await this.MigrateDefaultSceneAsync(project, context).ConfigureAwait(true);

        var scene = await ResolveInitialSceneAsync(this.projectContextService.ActiveProject ?? context).ConfigureAwait(true);
        if (scene is null)
        {
            return;
        }

        if (this.documentManager is null)
        {
            this.LogInitialSceneManagerUnavailable(scene.ToString());
            return;
        }

        var opened = await this.documentManager.OpenSceneAsync(scene).ConfigureAwait(true);
        if (!opened)
        {
            this.LogInitialSceneNotOpened(scene.ToString(), context.Name);
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
    private async Task MigrateDefaultSceneAsync(IProject project, ProjectContext context)
    {
        if (project.ProjectInfo.DefaultSceneId is not null)
        {
            return;
        }

        try
        {
            var usage = await this.projectUsage.GetProjectUsageAsync(context.Name, context.ProjectRoot).ConfigureAwait(true);
            if (string.IsNullOrWhiteSpace(usage?.LastOpenedScene))
            {
                return;
            }

            var lastOpened = usage.LastOpenedScene;
            if (await ResolveSceneByNameOrIdAsync(context, lastOpened).ConfigureAwait(true) is not { } migrated
                || await SceneFiles.ReadIdAsync(migrated.FilePath).ConfigureAwait(true) is not { } migratedId)
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

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Workspace activation must fail visibly without crashing the Project Browser when runtime startup fails.")]
    private async Task<bool> EnsureEngineRunningAsync()
    {
        await this.engineStartupGate.WaitAsync().ConfigureAwait(true);
        try
        {
            switch (this.engineService.State)
            {
                case EngineServiceState.Running:
                    await this.RestorePreviewSettingsAsync().ConfigureAwait(true);
                    return true;

                case EngineServiceState.NoEngine:
                case EngineServiceState.Faulted:
                    this.LogEngineStarting();
                    _ = await this.engineService.InitializeAsync().ConfigureAwait(true);
                    await this.RestorePreviewSettingsAsync().ConfigureAwait(true);
                    await this.engineService.StartAsync().ConfigureAwait(true);
                    return this.engineService.State == EngineServiceState.Running;

                case EngineServiceState.Ready:
                    this.LogInitializedEngineStarting();
                    await this.RestorePreviewSettingsAsync().ConfigureAwait(true);
                    await this.engineService.StartAsync().ConfigureAwait(true);
                    return this.engineService.State == EngineServiceState.Running;

                default:
                    this.LogEngineStateNotReady(this.engineService.State);
                    return false;
            }
        }
        catch (Exception ex)
        {
            this.LogEngineStartFailed(ex);
            RuntimeOperationResults.PublishFailure(
                this.operationResults,
                this.statusReducer,
                RuntimeOperationKinds.Start,
                FailureDomain.RuntimeDiscovery,
                DiagnosticCodes.RuntimePrefix + "START_FAILED",
                "Embedded runtime failed to start",
                "The workspace opened, but the live engine runtime could not start.",
                this.CreateProjectScope(),
                exception: ex);
            return false;
        }
        finally
        {
            _ = this.engineStartupGate.Release();
        }
    }

    private Task RestorePreviewSettingsAsync()
    {
        if (this.previewSettings is null || this.projectContextService.ActiveProject is not { } project)
        {
            throw new InvalidOperationException("The workspace preview preferences are not initialized.");
        }

        this.previewProject = project;
        return this.previewSettings.RestoreAsync(project);
    }

    private AffectedScope CreateProjectScope()
        => this.projectContextService.ActiveProject is { } project
            ? new AffectedScope
            {
                ProjectId = project.ProjectId,
                ProjectName = project.Name,
                ProjectPath = project.ProjectRoot,
            }
            : AffectedScope.Empty;

    private void PublishCookedRootWarning(
        string code,
        string title,
        string message,
        string affectedPath,
        Exception? exception = null)
        => RuntimeOperationResults.PublishWarning(
            this.operationResults,
            this.statusReducer,
            RuntimeOperationKinds.CookedRootRefresh,
            FailureDomain.AssetMount,
            code,
            title,
            message,
            this.CreateProjectScope(),
            affectedPath,
            exception);
}
