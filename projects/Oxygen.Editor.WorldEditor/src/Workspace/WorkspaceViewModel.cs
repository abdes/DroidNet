// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Output;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;

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
public partial class WorkspaceViewModel : DockingWorkspaceViewModel, IRecipient<AssetsCookedMessage>
{
    private readonly IContainer container;
    private readonly IProjectManagerService projectManager;
    private readonly IEngineService engineService;
    private readonly ILogger logger;
    private IMessenger? messenger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="container">The IoC container for dependency resolution.</param>
    /// <param name="router">The router for navigation within the workspace.</param>
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="engineService">The engine service for mounting cooked roots.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "will generate another warning due to capture of container arg")]
    public WorkspaceViewModel(
        IContainer container,
        IRouter router,
        IProjectManagerService projectManager,
        IEngineService engineService,
        ILoggerFactory? loggerFactory = null)
        : base(container, router, loggerFactory)
    {
        this.container = container;
        this.projectManager = projectManager;
        this.engineService = engineService;
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
            ]),
        },
    ]);

    /// <inheritdoc />
    public override async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        await base.OnNavigatedToAsync(route, navigationContext).ConfigureAwait(true);

        // Ensure messenger is available (resolve from container as fallback)
        this.messenger ??= this.container.Resolve<IMessenger>();
        this.messenger?.RegisterAll(this);

        // Mount the project's cooked assets root in the engine's virtual path resolver.
        // This allows the engine to resolve asset:/// URIs to actual files on disk.
        this.RefreshCookedRoots();
    }

    /// <inheritdoc />
    public void Receive(AssetsCookedMessage message)
    {
        this.logger.LogInformation("Received AssetsCookedMessage, refreshing cooked roots.");
        this.RefreshCookedRoots();
    }

    /// <inheritdoc />
    protected override void OnSetupChildContainer(IContainer childContainer)
    {
        childContainer.Register<IMessenger, StrongReferenceMessenger>(Reuse.Singleton);

        // Resolve messenger instance from the child container so the view model can use it.
        this.messenger = childContainer.Resolve<IMessenger>();

        // DocumentHostViewModel must be registered and resolved first to ensure it subscribes to
        // IDocumentService events before DocumentManager starts handling open requests.
        childContainer.Register<DocumentHostViewModel>(Reuse.Singleton);
        childContainer.Register<DocumentHostView>(Reuse.Singleton);

        // Resolve DocumentHostViewModel immediately to ensure it subscribes to document events
        _ = childContainer.Resolve<DocumentHostViewModel>();

        // DocumentManager must be a singleton and resolved immediately to ensure it is always listening for messages
        // even if the router hasn't navigated to any editor yet.
        childContainer.Register<DocumentManager>(Reuse.Singleton);
        _ = childContainer.Resolve<DocumentManager>();

        // Register shared services at workspace level
        childContainer.Register<ProjectAssetCatalog>(Reuse.Singleton);
        childContainer.RegisterMapping<IProjectAssetCatalog, ProjectAssetCatalog>();
        childContainer.RegisterMapping<IAssetCatalog, ProjectAssetCatalog>();

        // Register scene-engine synchronization service
        childContainer.Register<ISceneEngineSync, SceneEngineSync>(Reuse.Singleton);
        childContainer.Register<ISceneMutator, SceneMutator>(Reuse.Singleton);
        childContainer.Register<ISceneOrganizer, SceneOrganizer>(Reuse.Singleton);
        childContainer.Register<ISceneExplorerService, SceneExplorerService>(Reuse.Singleton);

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        childContainer.Register<OutputViewModel>(Reuse.Singleton);
        childContainer.Register<OutputView>(Reuse.Transient);

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<SceneEditorView>(Reuse.Transient);
        childContainer.Register<TransformViewModel>(Reuse.Transient);
        childContainer.Register<TransformView>(Reuse.Transient);

        childContainer.Register<GeometryViewModel>(Reuse.Transient);
        childContainer.Register<GeometryView>(Reuse.Transient);
    }

    /// <inheritdoc />
    protected override Task OnInitialNavigationAsync(ILocalRouterContext context)
        => context.LocalRouter.NavigateAsync(
            "/(renderer:dx//se:se;right;w=300//props:props;bottom=se//cb:cb;bottom;h=300//log:log;with=cb)");

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.engineService.UnmountProjectCookedRoot();
        }

        base.Dispose(disposing);
    }

    private void RefreshCookedRoots()
    {
        // Mount the project's cooked assets root in the engine's virtual path resolver.
        // This allows the engine to resolve asset:/// URIs to actual files on disk.
        if (this.projectManager.CurrentProject?.ProjectInfo.Location is null)
        {
            this.logger.LogWarning("Cannot refresh cooked roots: No current project or project location is null.");
            return;
        }

        // Source of Truth: Oxygen.Assets.AssetPipelineConstants
        const string CookedFolderName = ".cooked";
        const string IndexFileName = "container.index.bin";

        var projectLocation = this.projectManager.CurrentProject.ProjectInfo.Location;
        var cookedRoot = System.IO.Path.Combine(projectLocation, CookedFolderName);
        var indexPath = System.IO.Path.Combine(cookedRoot, IndexFileName);

        this.logger.LogInformation("Refreshing cooked roots. Project location: {ProjectLocation}", projectLocation);

        if (System.IO.File.Exists(indexPath))
        {
            this.logger.LogInformation("Mounting project cooked root: {CookedRoot}", cookedRoot);
            this.engineService.UnmountProjectCookedRoot();
            this.engineService.MountProjectCookedRoot(cookedRoot);
        }
        else
        {
            this.logger.LogWarning("Could not find cooked index file at: {IndexPath}. Assets will not be available in the engine.", indexPath);

            // Debug: list files in .cooked if it exists
            if (System.IO.Directory.Exists(cookedRoot))
            {
                var files = System.IO.Directory.GetFiles(cookedRoot, "*", System.IO.SearchOption.AllDirectories);
                this.logger.LogInformation("Files in {CookedRoot}: {Files}", cookedRoot, string.Join(", ", files));
            }
            else
            {
                this.logger.LogWarning("Cooked root directory does not exist: {CookedRoot}", cookedRoot);
            }
        }
    }
}
