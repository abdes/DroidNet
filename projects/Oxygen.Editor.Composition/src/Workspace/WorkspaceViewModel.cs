// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Workspace;

/// <summary>
///     The ViewModel of the world editor docking workspace.
/// </summary>
/// <remarks>
///     The workspace window has its own scope, created under the open project session's scope, and its own local router. Routes and
///     resolutions are isolated from the rest of the application: the view model resolves only through the window's
///     scope, and navigations, even the absolute ones, are relative to the workspace.
/// </remarks>
public partial class WorkspaceViewModel : DockingWorkspaceViewModel
{
    private readonly IReadOnlyList<IFeatureModule> modules;
    private readonly ProjectSessionHost sessions;
    private readonly IWindowManagerService windows;
    private readonly IProjectManagerService projectManager;
    private readonly IProjectUsageService projectUsage;
    private readonly ILogger logger;
    private IProjectSession? session;
    private IDocumentService? documentService;
    private IPlacementService? placement;
    private IAssetCatalog? assetCatalog;
    private PreviewSession? previewSession;
    private IProject? initialSceneProject;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="sessions">The project session, which creates the workspace window's scope and owns it.</param>
    /// <param name="windows">The window manager, which records the workspace window's session.</param>
    /// <param name="router">The router for navigation within the workspace.</param>
    /// <param name="projectManager">The project authoring service.</param>
    /// <param name="projectUsage">The per-project usage store (last-opened scene).</param>
    /// <param name="modules">The features whose panels and services each workspace window has.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    public WorkspaceViewModel(
        ProjectSessionHost sessions,
        IWindowManagerService windows,
        IRouter router,
        IProjectManagerService projectManager,
        IProjectUsageService projectUsage,
        IEnumerable<IFeatureModule> modules,
        ILoggerFactory? loggerFactory = null)
        : base(router, loggerFactory)
    {
        ArgumentNullException.ThrowIfNull(modules);
        this.modules = [.. modules];
        this.sessions = sessions;
        this.windows = windows;
        this.RoutesConfig = CreateRoutes(this.modules);
        this.projectManager = projectManager;
        this.projectUsage = projectUsage;
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
        if (this.previewSession is { } preview && this.session is { } session)
        {
            _ = await preview.StartAsync(session.Context).ConfigureAwait(true);
        }

        await this.OpenInitialSceneAsync().ConfigureAwait(true);
    }

    /// <inheritdoc />
    protected override ScopeHandle CreateWindowScope(IReadOnlyList<IScopeModule<IWindowLayer>> modules, Action<IRegistrator>? hostInstances)
        => this.sessions.CreateWindowScope(modules, hostInstances);

    /// <inheritdoc />
    protected override IEnumerable<IScopeModule<IWindowLayer>> CreateWindowModules()
        => WorkspaceWindowModule.Compose(this.modules);

    /// <inheritdoc />
    protected override void OnWindowScopeOpened(IResolver window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The window is one of the session's: closing the session's last window returns to the Start window and closes
        // the session.
        this.session = window.Resolve<IProjectSession>();
        this.windows.SetMetadata(window.Resolve<WindowId>(), EditorWindows.SessionMetadataKey, this.session);
        this.assetCatalog = window.Resolve<IAssetCatalog>();
        this.documentService = window.Resolve<IDocumentService>();
        this.placement = window.Resolve<IPlacementService>();
        this.previewSession = window.Resolve<PreviewSession>();
        this.ObservePreviewSession();
        this.playSession = window.Resolve<PlaySession>();
        this.scriptTooling = window.Resolve<ProjectScriptTooling>();
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
            this.placement = null;
            this.assetCatalog = null;
            this.session = null;
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

    private async Task OpenInitialSceneAsync()
    {
        if (this.session is not { } session
            || this.assetCatalog is not { } catalog
            || !session.Context.OpenInitialScene
            || ReferenceEquals(session.Project, this.initialSceneProject))
        {
            return;
        }

        var project = session.Project;
        var context = session.Context;

        this.initialSceneProject = project;

        // Seed the default scene for projects that predate DefaultSceneId before resolving it.
        await this.MigrateDefaultSceneAsync(catalog, project, context).ConfigureAwait(true);

        var scene = await SceneFiles.FindInitialAsync(catalog, session.Context).ConfigureAwait(true);
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
