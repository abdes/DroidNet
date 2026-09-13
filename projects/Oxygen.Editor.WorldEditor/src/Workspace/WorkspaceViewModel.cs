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
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
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
    private IMessenger? messenger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="container">The IoC container for dependency resolution.</param>
    /// <param name="router">The router for navigation within the workspace.</param>
    /// <param name="projectContextService">The active project context service.</param>
    /// <param name="projectManager">The project authoring service.</param>
    /// <param name="projectUsage">The recent-scene restoration store.</param>
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
        this.documentManager = childContainer.Resolve<DocumentManager>();

        // Register shared services at workspace level
        childContainer.Register<Oxygen.Editor.ContentPipeline.Discovery.IBuiltinCatalogDiscovery, Oxygen.Editor.ContentPipeline.Discovery.BuiltinCatalogDiscovery>(Reuse.Singleton);
        childContainer.Register<ProjectAssetCatalog>(Reuse.Singleton);
        childContainer.RegisterMapping<IProjectAssetCatalog, ProjectAssetCatalog>();
        childContainer.RegisterMapping<IAssetCatalog, ProjectAssetCatalog>();
        childContainer.Register<IAssetIdentityReducer, AssetIdentityReducer>(Reuse.Singleton);
        childContainer.RegisterDelegate<Oxygen.Editor.ContentPipeline.Status.IAssetCookStatusReader>(
            resolver => resolver.Resolve<Oxygen.Editor.ContentPipeline.IContentPipelineService>(), Reuse.Singleton);
        childContainer.Register<IContentBrowserAssetProvider, ContentBrowserAssetProvider>(Reuse.Singleton);
        childContainer.Register<IMaterialPickerService, MaterialPickerService>(Reuse.Singleton);

        // Register scene-engine synchronization service
        childContainer.Register<ISceneEngineSync, SceneEngineSync>(Reuse.Singleton);
        childContainer.Register<ISceneMutator, SceneMutator>(Reuse.Singleton);
        childContainer.Register<ISceneOrganizer, SceneOrganizer>(Reuse.Singleton);
        childContainer.Register<ISceneExplorerService, SceneExplorerService>(Reuse.Singleton);
        childContainer.Register<ISceneSelectionService, SceneSelectionService>(Reuse.Singleton);
        childContainer.Register<ISceneDocumentCommandService, SceneDocumentCommandService>(Reuse.Singleton);

        childContainer.Register<SceneExplorerViewModel>(Reuse.Transient);
        childContainer.Register<SceneExplorerView>(Reuse.Transient);
        childContainer.Register<ContentBrowserViewModel>(Reuse.Transient);
        childContainer.Register<ContentBrowserView>(Reuse.Transient);
        this.RegisterOutputPanels(childContainer);

        childContainer.Register<SceneNodeEditorViewModel>(Reuse.Transient);
        childContainer.Register<SceneNodeEditorView>(Reuse.Transient);
        childContainer.Register<SceneEditorView>(Reuse.Transient);
        childContainer.Register<MaterialEditorView>(Reuse.Transient);
        childContainer.Register<TransformViewModel>(Reuse.Transient);
        childContainer.Register<TransformView>(Reuse.Transient);
        childContainer.Register<PerspectiveCameraViewModel>(Reuse.Transient);
        childContainer.Register<PerspectiveCameraView>(Reuse.Transient);
        childContainer.Register<DirectionalLightViewModel>(Reuse.Transient);
        childContainer.Register<DirectionalLightView>(Reuse.Transient);
        childContainer.Register<EnvironmentViewModel>(Reuse.Transient);
        childContainer.Register<EnvironmentView>(Reuse.Transient);

        childContainer.Register<GeometryViewModel>(Reuse.Transient);
        childContainer.Register<GeometryView>(Reuse.Transient);
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

    private static void ValidateCookedIndexFiles(Oxygen.Managed.Assets.Persistence.LooseCooked.V1.Document document, string cookedRoot)
    {
        foreach (var asset in document.Assets)
        {
            if (string.IsNullOrWhiteSpace(asset.DescriptorRelativePath))
            {
                throw new InvalidDataException("Cooked index contains an asset without a descriptor path.");
            }

            var descriptorPath = System.IO.Path.Combine(
                cookedRoot,
                asset.DescriptorRelativePath.Replace('/', System.IO.Path.DirectorySeparatorChar));
            if (!System.IO.File.Exists(descriptorPath))
            {
                throw new FileNotFoundException("Cooked asset descriptor is missing.", descriptorPath);
            }

            var actualSize = new System.IO.FileInfo(descriptorPath).Length;
            if (actualSize != (long)asset.DescriptorSize)
            {
                throw new InvalidDataException(
                    string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Cooked descriptor size mismatch for '{asset.DescriptorRelativePath}': expected {asset.DescriptorSize}, found {actualSize}."));
            }
        }

        foreach (var file in document.Files)
        {
            if (string.IsNullOrWhiteSpace(file.RelativePath))
            {
                throw new InvalidDataException("Cooked index contains a file record without a path.");
            }

            var filePath = System.IO.Path.Combine(
                cookedRoot,
                file.RelativePath.Replace('/', System.IO.Path.DirectorySeparatorChar));
            if (!System.IO.File.Exists(filePath))
            {
                throw new FileNotFoundException("Cooked file record is missing.", filePath);
            }

            var actualSize = new System.IO.FileInfo(filePath).Length;
            if (actualSize != (long)file.Size)
            {
                throw new InvalidDataException(
                    string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Cooked file size mismatch for '{file.RelativePath}': expected {file.Size}, found {actualSize}."));
            }
        }
    }

    private static bool IsUnderRoot(string candidatePath, string rootPath)
    {
        var normalizedCandidate = System.IO.Path.GetFullPath(candidatePath)
            .TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);
        var normalizedRoot = System.IO.Path.GetFullPath(rootPath)
            .TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);

        return normalizedCandidate.Equals(normalizedRoot, StringComparison.OrdinalIgnoreCase)
               || normalizedCandidate.StartsWith(
                   normalizedRoot + System.IO.Path.DirectorySeparatorChar,
                   StringComparison.OrdinalIgnoreCase)
               || normalizedCandidate.StartsWith(
                   normalizedRoot + System.IO.Path.AltDirectorySeparatorChar,
                   StringComparison.OrdinalIgnoreCase);
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

        IDisposable? reader = null;
        try
        {
            reader = await this.container.Resolve<Oxygen.Editor.ContentPipeline.Publication.CookPublicationService>()
                .AcquireForMountAsync(project, CancellationToken.None).ConfigureAwait(true);
            var root = Path.Combine(project.ProjectRoot, ".cooked");
            var candidates = File.Exists(Path.Combine(root, "container.index.bin"))
                ? new[] { root }
                : this.GetCookedMountPoints(project, root).Select(mount => Path.Combine(root, mount))
                    .Where(path => File.Exists(Path.Combine(path, "container.index.bin"))).ToArray();
            var roots = this.GetMountableRoots(candidates, root);
            var acceptedReader = reader;
            reader = null;
            await this.engineService.RefreshProjectCookedRootsAsync(roots, acceptedReader).ConfigureAwait(true);
            this.LogMountedRoots(roots);
        }
        catch (Exception exception)
        {
            this.PublishCookedRootWarning(AssetMountDiagnosticCodes.RefreshFailed, "Cooked content is unavailable", exception.Message, project.ProjectRoot, exception);
            await this.engineService.SuspendCookedContentAsync().ConfigureAwait(true);
        }
        finally
        {
            reader?.Dispose();
        }
    }

    private HashSet<string> GetCookedMountPoints(ProjectContext activeProject, string cookedBaseRoot)
    {
        var mountPoints = new HashSet<string>(StringComparer.Ordinal);
        foreach (var mount in activeProject.AuthoringMounts)
        {
            if (!string.IsNullOrWhiteSpace(mount.Name))
            {
                _ = mountPoints.Add(mount.Name);
            }
        }

        try
        {
            if (!Directory.Exists(cookedBaseRoot))
            {
                return mountPoints;
            }

            foreach (var dir in System.IO.Directory.GetDirectories(cookedBaseRoot))
            {
                var name = System.IO.Path.GetFileName(dir);
                if (!string.IsNullOrWhiteSpace(name))
                {
                    _ = mountPoints.Add(name);
                }
            }
        }
        catch (Exception ex) when (ex is IOException
            or UnauthorizedAccessException
            or ArgumentException
            or NotSupportedException)
        {
            this.LogMountEnumerationFailed(ex, cookedBaseRoot);
            this.PublishCookedRootWarning(
                AssetMountDiagnosticCodes.RefreshFailed,
                "Cooked roots could not be enumerated",
                "The workspace opened, but cooked assets may not be available because the cooked root folders could not be enumerated.",
                cookedBaseRoot,
                ex);
        }

        return mountPoints;
    }

    private List<string> GetMountableRoots(IReadOnlyList<string> normalizedRoots, string cookedBaseRoot)
    {
        var mountableRoots = new List<string>();
        foreach (var root in normalizedRoots)
        {
            if (!IsUnderRoot(root, cookedBaseRoot))
            {
                this.PublishCookedRootWarning(
                    AssetMountDiagnosticCodes.RefreshFailed,
                    "Validated cooked root was rejected",
                    "The validated cooked root is outside the active project's cooked output directory.",
                    root);
                continue;
            }

            var indexPath = System.IO.Path.Combine(root, "container.index.bin");
            if (!System.IO.File.Exists(indexPath))
            {
                this.PublishCookedRootWarning(
                    AssetMountDiagnosticCodes.RefreshFailed,
                    "Validated cooked index is missing",
                    "The validated cooked root cannot be mounted because its loose cooked index is missing.",
                    root);
                continue;
            }

            if (!this.IsCookedIndexMountable(indexPath))
            {
                this.PublishCookedRootWarning(
                    AssetMountDiagnosticCodes.RefreshFailed,
                    "Validated cooked index is incompatible",
                    "The validated cooked root cannot be mounted because its loose cooked index could not be read.",
                    indexPath);
                continue;
            }

            mountableRoots.Add(root);
        }

        return mountableRoots;
    }

    private async Task OpenInitialSceneAsync()
    {
        if (this.messenger is null
            || this.projectManager.CurrentProject is not { } project
            || this.projectContextService.ActiveProject is not { } context
            || project.Scenes.Count == 0
            || !context.OpenInitialScene)
        {
            return;
        }

        var scene = await this.ResolveInitialSceneAsync(project, context).ConfigureAwait(true);
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

    private async Task<Oxygen.Editor.World.Scene?> ResolveInitialSceneAsync(IProject project, ProjectContext context)
    {
        if (context.InitialSceneAssetUri is { } initialSceneAssetUri
            && TryResolveSceneFromAssetUri(project, initialSceneAssetUri) is { } starterScene)
        {
            return starterScene;
        }

        try
        {
            var usage = await this.projectUsage.GetProjectUsageAsync(context.Name, context.ProjectRoot).ConfigureAwait(true);
            if (!string.IsNullOrWhiteSpace(usage?.LastOpenedScene))
            {
                var lastOpenedScene = usage.LastOpenedScene;
                var lastOpenedSceneName = System.IO.Path.GetFileNameWithoutExtension(lastOpenedScene);
                var restored = project.Scenes.FirstOrDefault(scene =>
                    string.Equals(scene.Name, lastOpenedScene, StringComparison.OrdinalIgnoreCase)
                    || string.Equals(scene.Name, lastOpenedSceneName, StringComparison.OrdinalIgnoreCase)
                    || string.Equals(scene.Id.ToString("D"), lastOpenedScene, StringComparison.OrdinalIgnoreCase));

                if (restored is not null)
                {
                    return restored;
                }
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneRestorationFailed(ex, context.Name);
        }

        return project.ActiveScene;
    }

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Workspace activation must fail visibly without crashing the Project Browser when runtime startup fails.")]
    private async Task<bool> EnsureEngineRunningAsync()
    {
        if (this.engineService.State == EngineServiceState.Running)
        {
            return true;
        }

        await this.engineStartupGate.WaitAsync().ConfigureAwait(true);
        try
        {
            switch (this.engineService.State)
            {
                case EngineServiceState.Running:
                    return true;

                case EngineServiceState.NoEngine:
                case EngineServiceState.Faulted:
                    this.LogEngineStarting();
                    _ = await this.engineService.InitializeAsync().ConfigureAwait(true);
                    await this.engineService.StartAsync().ConfigureAwait(true);
                    return this.engineService.State == EngineServiceState.Running;

                case EngineServiceState.Ready:
                    this.LogInitializedEngineStarting();
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

    private bool IsCookedIndexMountable(string indexPath)
    {
        try
        {
            using var stream = System.IO.File.OpenRead(indexPath);
            var document = LooseCookedIndex.Read(stream);
            var cookedRoot = System.IO.Path.GetDirectoryName(indexPath)
                ?? throw new InvalidDataException("Cooked index path has no parent directory.");
            ValidateCookedIndexFiles(document, cookedRoot);

            return true;
        }
        catch (Exception ex) when (ex is InvalidDataException
            or NotSupportedException
            or FileNotFoundException
            or IOException
            or UnauthorizedAccessException
            or ArgumentException
            or FormatException)
        {
            this.LogCookedIndexRejected(ex, indexPath);
            return false;
        }
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
