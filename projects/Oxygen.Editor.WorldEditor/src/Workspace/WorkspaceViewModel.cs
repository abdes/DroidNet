// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
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
using Oxygen.Editor.World.Output;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Assets.Catalog;
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

    /// <summary>Resolves a scene from a last-opened name by display name, file stem, or stable ID.</summary>
    /// <param name="project">The project whose scenes are searched.</param>
    /// <param name="lastOpenedScene">The persisted last-opened scene name.</param>
    /// <returns>The matching scene, or <see langword="null"/> when no scene matches.</returns>
    internal static Oxygen.Editor.World.Scene? ResolveSceneByNameOrId(IProject project, string? lastOpenedScene)
    {
        if (string.IsNullOrWhiteSpace(lastOpenedScene))
        {
            return null;
        }

        var sceneName = lastOpenedScene.EndsWith(Oxygen.Editor.Projects.Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase)
            ? lastOpenedScene[..^Oxygen.Editor.Projects.Constants.SceneFileExtension.Length]
            : System.IO.Path.GetFileNameWithoutExtension(lastOpenedScene);
        return project.Scenes.FirstOrDefault(scene =>
            string.Equals(scene.Name, lastOpenedScene, StringComparison.OrdinalIgnoreCase)
            || string.Equals(scene.Name, sceneName, StringComparison.OrdinalIgnoreCase)
            || string.Equals(scene.Id.ToString("D"), lastOpenedScene, StringComparison.OrdinalIgnoreCase));
    }

    internal static Oxygen.Editor.World.Scene? ResolveInitialScene(IProject project, ProjectContext context)
    {
        // Fresh activation may carry an explicit scene request (template StarterScene or a workflow open).
        if (context.InitialSceneAssetUri is { } initialSceneAssetUri
            && TryResolveSceneFromAssetUri(project, initialSceneAssetUri) is { } starterScene)
        {
            return starterScene;
        }

        // Otherwise the single loaded scene is the project's configured default, resolved by stable ID.
        // A missing or invalid default is not silently replaced by last-opened or first-listed.
        if (project.ProjectInfo.DefaultSceneId is { } defaultSceneId
            && project.Scenes.FirstOrDefault(scene => scene.Id == defaultSceneId) is { } defaultScene)
        {
            return defaultScene;
        }

        return null;
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
        childContainer.Register<DocumentManager>(Reuse.Singleton);
        this.documentManager = childContainer.Resolve<DocumentManager>();

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        this.RegisterOutputPanels(childContainer);

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
            "/(renderer:dx//se:se;right;w=350//props:props;bottom=se//cb:cb;bottom;h=340//log:log;with=cb//cook:cook;with=cb)");

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
        childContainer.Register<ProjectAssetCatalog>(Reuse.Singleton);
        childContainer.RegisterMapping<IProjectAssetCatalog, ProjectAssetCatalog>();
        childContainer.RegisterMapping<IAssetCatalog, ProjectAssetCatalog>();
        childContainer.Register<IAssetIdentityReducer, AssetIdentityReducer>(Reuse.Singleton);
        childContainer.RegisterDelegate<Oxygen.Editor.ContentPipeline.Status.IAssetCookStatusReader>(
            resolver => resolver.Resolve<Oxygen.Editor.ContentPipeline.IContentPipelineService>(), Reuse.Singleton);
        childContainer.Register<IContentBrowserAssetProvider, ContentBrowserAssetProvider>(Reuse.Singleton);
        childContainer.Register<IMaterialPickerService, MaterialPickerService>(Reuse.Singleton);
        childContainer.Register<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService, Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationService>(Reuse.Singleton);

        // One instance for the workspace: open documents register as participants on it, and the Content Browser's
        // child container must reach the same instance instead of creating its own on first use.
        _ = childContainer.Resolve<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>();
    }

    private static Oxygen.Editor.World.Scene? TryResolveSceneFromAssetUri(IProject project, Uri sceneAssetUri)
    {
        if (!string.Equals(sceneAssetUri.Scheme, "asset", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var fileName = System.IO.Path.GetFileName(Uri.UnescapeDataString(sceneAssetUri.AbsolutePath));
        if (string.IsNullOrWhiteSpace(fileName))
        {
            return null;
        }

        var sceneName = fileName.EndsWith(Oxygen.Editor.Projects.Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase)
            ? fileName[..^Oxygen.Editor.Projects.Constants.SceneFileExtension.Length]
            : System.IO.Path.GetFileNameWithoutExtension(fileName);

        return project.Scenes.FirstOrDefault(scene =>
            string.Equals(scene.Name, sceneName, StringComparison.OrdinalIgnoreCase)
            || string.Equals(scene.Id.ToString("D"), sceneName, StringComparison.OrdinalIgnoreCase));
    }

    private void RegisterOutputPanels(IContainer childContainer)
    {
        this.cookHosting = childContainer.Resolve<DroidNet.Hosting.WinUI.HostingContext>();
        this.cookedCatalog = childContainer.Resolve<IProjectAssetCatalog>();
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
            using var reader = await publication.AcquireForMountAsync(project, CancellationToken.None).ConfigureAwait(true);
            var bindings = reader.Roots.Zip(
                reader.RootPaths,
                static (root, path) => new RuntimeCookedRoot(path, root.Owner == Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project ? root.Name : null)).ToArray();
            var mounts = await mountService.PrepareAsync(project, reader, CancellationToken.None).ConfigureAwait(true);
            stage = "native mount and preview resume";
            await this.engineService.RefreshProjectCookedRootsAsync(bindings, mounts).ConfigureAwait(true);
            mounted = true;
            stage = "asset catalog refresh";
            var catalog = this.cookedCatalog ?? throw new InvalidOperationException("The workspace asset catalog is not initialized.");
            await catalog.RefreshAsync(reader, CancellationToken.None).ConfigureAwait(true);
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
            || project.Scenes.Count == 0
            || !context.OpenInitialScene
            || ReferenceEquals(project, this.initialSceneProject))
        {
            return;
        }

        this.initialSceneProject = project;

        // Seed the default scene for projects that predate DefaultSceneId before resolving it.
        await this.MigrateDefaultSceneAsync(project, context).ConfigureAwait(true);

        var scene = ResolveInitialScene(project, context);
        if (scene is null)
        {
            return;
        }

        if (this.documentManager is null)
        {
            this.LogInitialSceneManagerUnavailable(scene.Name);
            return;
        }

        var opened = await this.documentManager.OpenSceneAsync(scene).ConfigureAwait(true);
        if (!opened)
        {
            this.LogInitialSceneNotOpened(scene.Name, context.Name);
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
        if (project.ProjectInfo.DefaultSceneId is not null || project.Scenes.Count == 0)
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
            var migrated = ResolveSceneByNameOrId(project, lastOpened);
            if (migrated is null)
            {
                return;
            }

            project.ProjectInfo.DefaultSceneId = migrated.Id;
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
