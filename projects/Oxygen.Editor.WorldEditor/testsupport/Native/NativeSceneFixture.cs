// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Mvvm;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using DryIoc;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspection;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class NativeSceneFixture : IAsyncDisposable
{
    public CookingPanelViewModel CreateTimingCookingPanel(CatalogWorkloadServices services) => new(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), this.hosting);
    public ICookDocumentRegistration RegisterMainCookDocument(CatalogWorkloadServices services)
    {
        var source = this.manager.GetSceneSourceVersion(this.Source)!;
        return services.Documents.Register(source.SourcePath, token => this.hosting.Dispatcher.DispatchAsync(() => this.Commands.AcquireCookReadAsync(this.Context, token)));
    }

    public async Task SetSavedLegacyAerialStartAsync()
    {
        this.Model.SetScene(value: null);
        var environment = this.Source.Environment;
        this.Source.Hydrate(this.Source.Dehydrate() with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = -1 } } });
        _ = (await this.Commands.SaveSceneAsync(this.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        this.Model.SetScene(this.Source);
    }

    public async Task<(Uri uri, string key)> CookTestMaterialAsync(string name, CancellationToken cancellationToken, System.Numerics.Vector4? baseColor = null, string? projectRoot = null)
    {
        var root = projectRoot ?? this.ProjectRoot;
        var relative = $"Content/Materials/{name}.omat.json";
        var path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var color = baseColor ?? System.Numerics.Vector4.One;
        var factor = System.Text.Json.JsonSerializer.Serialize(new[] { color.X, color.Y, color.Z, color.W });
        var source = $$"""
                { "name": "{{name}}",
                  "parameters": { "base_color": {{factor}}, "metalness": 0, "roughness": 0.5 } }
                """;
        await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
        var native = this.MaterialPipelineFor(root);
        var uri = new Uri($"asset:///{relative}");
        var result = await native.Pipeline.CookAssetAsync(uri, cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var cookedRoot = await this.GetCookedRootAsync(root, cancellationToken).ConfigureAwait(true);
        using var indexStream = File.OpenRead(Path.Combine(cookedRoot, "container.index.bin"));
        var asset = LooseCookedIndex.Read(indexStream).Assets.Single(value => value.VirtualPath == $"/Content/Materials/{name}.omat");
        if (projectRoot is null)
        {
            var row = new MaterialPickerResult(uri, name, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Mounted, path, Path.Combine(cookedRoot, "Materials", $"{name}.omat"), BaseColorPreview: null);
            this.SetMaterialChoices(this.materialChoices.Value.Append(row).ToArray());
        }

        var keyBytes = new byte[16];
        asset.AssetKey.WriteBytes(keyBytes);
        return (uri, new Guid(keyBytes, bigEndian: true).ToString());
    }

    public async Task ApplyContentPriorityAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var current = this.projectContexts.ActiveProject!;
        var desired = new ProjectInfo(project.ProjectId, project.Name, project.Category, project.ProjectRoot, project.Thumbnail)
        {
            AuthoringMounts = [.. project.AuthoringMounts],
            LocalFolderMounts = [.. project.LocalFolderMounts],
            CookedContentOrder = [.. project.CookedContentOrder],
        };
        var catalog = new Mock<IProjectAssetCatalog>();
        _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<CookPublicationReadLease>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var registration = this.materialPipeline.Publication.RegisterPreview(current, () => Task.FromResult<ICookPublicationPreview?>(new WorkspacePublicationPreview(current, this.engine, this.hosting, catalog.Object, this.messenger, () => ReferenceEquals(current, this.projectContexts.ActiveProject), static () =>
        {
        })));
        var service = new ContentMountChangeService(this.materialCookCoordinator, this.projectContexts, this.materialPipeline.Publication, catalog.Object, this.hosting);
        await service.ApplyAsync(current, desired, next =>
        {
            var info = this.Source.Project.ProjectInfo;
            info.LocalFolderMounts.Clear();
            foreach (var mount in next.LocalFolderMounts)
            {
                info.LocalFolderMounts.Add(mount);
            }

            info.CookedContentOrder.Clear();
            foreach (var entry in next.CookedContentOrder)
            {
                info.CookedContentOrder.Add(entry);
            }
        }, cancellationToken).ConfigureAwait(true);
    }

    public DocumentHostViewModel CreateDocumentTransitionHost(IContainer container, EditorDocumentService documents, CatalogWorkloadServices services, WindowId windowId)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var appearance = new Mock<ISettingsService<IAppearanceSettings>>();
        _ = appearance.SetupGet(value => value.Settings).Returns(new AppearanceSettings());
        container.RegisterInstance(appearance.Object);
        container.RegisterInstance<IMessenger>(this.messenger);
        container.RegisterInstance<ISceneEngineSync>(this.sync);
        container.RegisterInstance<ISceneDocumentCommandService>(this.Commands);
        container.RegisterInstance(Mock.Of<IDocumentInputCommitter>());
        container.RegisterInstance(Mock.Of<IDocumentConflictPrompt>());
        container.RegisterInstance<IContentPipelineService>(services.Pipeline);
        container.RegisterInstance<IProjectContextService>(services.Projects);
        container.RegisterInstance<IContentBrowserAssetProvider>(this.AssetCatalog.Object);
        container.RegisterInstance(new SceneCookInputRegistrar(services.Documents, this.manager, this.hosting, documents));
        container.RegisterInstance(new Oxygen.Editor.World.Workspace.PreviewSettingsService(this.engine, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), services.Projects, publisher.Object, new OperationStatusReducer()));
        var views = new Mock<IViewLocator>();
        _ = views.Setup(value => value.ResolveView(It.IsAny<object>())).Returns((object model) => model is SceneEditorViewModel ? new SceneEditorView() : new CookedInspectionView());
        return new(documents, views.Object, this.engine, publisher.Object, new OperationStatusReducer(), container, new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), publisher.Object), Mock.Of<IWindowManagerService>(), windowId);
    }

    public DocumentManager CreateTransitionDocumentManager(EditorDocumentService documents, IProjectContextService projects, WindowId windowId) => new(documents, this.messenger, projects, Mock.Of<Oxygen.Editor.Data.Services.IProjectUsageService>(), Mock.Of<IMaterialDocumentService>(), windowId);
    public void NotifyTransitionSceneReady() => this.messenger.Send(new SceneLoadedMessage(this.Source));
    public Task<bool> OpenTransitionInspectionAsync(ProjectContext project) => this.messenger.Send(new OpenCookedInspectionRequestMessage(project, new Uri("asset:///Content/Geometry"), validate: false)).Response;
    public SceneContentDemandService CreateImportedAssetDemand(CatalogWorkloadServices services, ContentBrowserAssetProvider provider)
    {
        var demand = new SceneContentDemandService(this.hosting, provider, services.Pipeline, services.Projects, this.documents.Object, this.sync, Mock.Of<ISceneExplorerService>(), this.messenger, default, NullLogger<SceneContentDemandService>.Instance);
        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(this.Source, this.Context.Metadata));
        return demand;
    }

    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenEnvironmentField-");
    private readonly ProjectManagerService manager = new(new NativeStorageProvider(new RealFileSystem()));
    private readonly EngineService engine;
    private readonly Oxygen.Testing.TemporaryNativeArtifacts compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
    private readonly SceneEngineSync sync;
    private readonly ProjectContextService projectContexts = new();
    private readonly ContentCookCoordinator materialCookCoordinator;
    private readonly Oxygen.Testing.NativeContentPipelineFixture materialPipeline;
    private readonly Dictionary<string, (ContentCookCoordinator Coordinator, Oxygen.Testing.NativeContentPipelineFixture Pipeline)> foreignMaterialPipelines = new(StringComparer.OrdinalIgnoreCase);
    private readonly Mock<IDocumentService> documents = new();
    private readonly StrongReferenceMessenger messenger = new();
    private readonly HostingContext hosting;
    private readonly BehaviorSubject<IReadOnlyList<MaterialPickerResult>> materialChoices = new([]);
    private RuntimeSceneTarget? target;
    public NativeSceneFixture(bool automatic, Action<Scene>? seed = null, EngineSettings? engineSettings = null, ILoggerFactory? loggerFactory = null)
    {
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        this.hosting = new HostingContext
        {
            Application = Application.Current,
            Dispatcher = dispatcher,
            DispatcherScheduler = new DispatcherQueueScheduler(dispatcher),
            IsRunning = true
        };
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var results = publisher.Object;
        var settings = new Mock<DroidNet.Config.ISettingsService<IEngineSettings>>();
        _ = settings.SetupGet(value => value.Settings).Returns(engineSettings ?? new EngineSettings());
        this.engine = new EngineService(this.hosting, results, loggerFactory, engineSettings: settings.Object, nativeCompatibility: this.compatibility);
        this.sync = new SceneEngineSync(this.engine, operationResults: results, hostingContext: this.hosting);
        var project = new Project(new ProjectInfo("Environment fields", Category.Games, this.directory.FullName, "preview.png") { AuthoringMounts = [new("Content", "Content")], })
        {
            Name = "Environment fields"
        };
        File.WriteAllText(Path.Combine(this.directory.FullName, "Project.oxy"), ProjectInfo.ToJson(project.ProjectInfo));
        this.projectContexts.Activate(ProjectContext.FromProjectInfo(project.ProjectInfo));
        this.materialCookCoordinator = new(this.projectContexts, NullLogger<ContentCookCoordinator>.Instance);
        this.materialPipeline = new(this.projectContexts, this.materialCookCoordinator, new CookDocumentRegistry());
        var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
        this.Source = Scene.CreateAndHydrate(project, new SceneData { Id = Guid.NewGuid(), Name = "Environment", Environment = new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } }, });
        seed?.Invoke(this.Source);
        project.Scenes.Add(this.Source);
        this.Context = new(this.Source.Id, new SceneDocumentMetadata(this.Source.Id), this.Source, UndoRedo.GetHistory(this.Source.Id));
        _ = this.documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
        _ = this.documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns(() => [this.Context.Metadata]);
        _ = this.documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(() => this.Source.Id);
        _ = this.AssetCatalog.Setup(value => value.Items).Returns(Observable.Empty<IReadOnlyList<Oxygen.Editor.ContentBrowser.AssetIdentity.ContentBrowserAssetItem>>());
        _ = this.AssetCatalog.Setup(value => value.RefreshAsync(It.IsAny<Oxygen.Editor.ContentBrowser.AssetIdentity.AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = this.MaterialPicker.Setup(value => value.Results).Returns(this.materialChoices);
        _ = this.MaterialPicker.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        this.Commands = new SceneDocumentCommandService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), Mock.Of<ISceneExplorerService>(), new SceneSelectionService(), this.sync, this.manager, this.documents.Object, default, this.messenger, results, new OperationStatusReducer(), this.materialPipeline.Pipeline, this.projectContexts);
        this.Model = new EnvironmentViewModel(this.Commands, () => this.Context);
        this.Model.SetScene(this.Source);
        this.sync.SceneSynchronized += (_, args) =>
        {
            if (ReferenceEquals(args.Metadata, this.Context.Metadata))
            {
                this.target = args.Target;
            }
        };
    }

    public Scene Source { get; private set; }
    public SceneDocumentCommandContext Context { get; private set; }
    public EnvironmentViewModel Model { get; }
    public SceneDocumentCommandService Commands { get; }
    public Mock<Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider> AssetCatalog { get; } = new();
    public Mock<IMaterialPickerService> MaterialPicker { get; } = new();
    public ConcurrentQueue<OperationResult> Results { get; } = new();
    public string ProjectRoot => this.directory.FullName;
    public ProjectContextService Projects => this.projectContexts;

    public async Task<MaterialSlotTarget> ReadSingleMaterialSlotAsync(Guid nodeId, CancellationToken cancellationToken)
    {
        var node = this.Source.RootNodes.Select(root => SceneTraversal.FindNodeById(root, nodeId)).OfType<SceneNode>().Single();
        var uri = node.Components.OfType<GeometryComponent>().Single().Geometry!.Uri;
        var inventory = await ((IGeometryMaterialSlotProvider)this.materialPipeline.Pipeline).ReadAsync(this.projectContexts.ActiveProject!, uri, cancellationToken).ConfigureAwait(true);
        _ = inventory.Should().NotBeNull("the native fixture geometry must have a current slot inventory");
        var slot = inventory!.Slots.Should().ContainSingle().Which;
        return new(uri, slot.SlotId, inventory.LayoutRevision);
    }

    private Oxygen.Testing.NativeContentPipelineFixture MaterialPipelineFor(string root)
    {
        if (string.Equals(root, this.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            return this.materialPipeline;
        }

        if (!this.foreignMaterialPipelines.TryGetValue(root, out var value))
        {
            var contexts = new ProjectContextService();
            var info = new ProjectInfo("Foreign material fixture", Category.Games, root)
            {
                AuthoringMounts = [new("Content", "Content")],
            };
            File.WriteAllText(Path.Combine(root, "Project.oxy"), ProjectInfo.ToJson(info));
            contexts.Activate(ProjectContext.FromProjectInfo(info));
            var coordinator = new ContentCookCoordinator(contexts, NullLogger<ContentCookCoordinator>.Instance);
            value = (coordinator, new(contexts, coordinator, new CookDocumentRegistry()));
            this.foreignMaterialPipelines.Add(root, value);
        }

        return value.Pipeline;
    }

    public void SetMaterialChoices(IReadOnlyList<MaterialPickerResult> choices) => this.materialChoices.OnNext(choices);
    public async Task<string> GetCookedRootAsync(string projectRoot, CancellationToken token)
    {
        var info = await this.manager.LoadProjectInfoAsync(projectRoot).ConfigureAwait(true) ?? throw new InvalidOperationException("The native fixture project has not been saved.");
        using var publication = await this.MaterialPipelineFor(projectRoot).Publication.AcquireReadAsync(ProjectContext.FromProjectInfo(info), token).ConfigureAwait(true);
        return publication.FindProjectRoot("Content") ?? throw new InvalidOperationException("The fixture has not cooked its Content mount.");
    }

    public async Task RefreshCookedRootsAsync(bool mountPublished = true)
    {
        using var selected = await this.materialPipeline.Publication.AcquireReadAsync(this.projectContexts.ActiveProject!, CancellationToken.None).ConfigureAwait(true);
        var bindings = mountPublished ? selected.Roots.Zip(selected.RootPaths).Select(static entry => new RuntimeCookedRoot(entry.Second, entry.First.Owner == Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project ? entry.First.Name : null)).ToArray() : [];
        await this.engine.RefreshProjectCookedRootsAsync(bindings, selected.Retain()).ConfigureAwait(true);
    }

    public Task SuspendCookedContentAsync() => this.engine.SuspendCookedContentAsync();
    public SceneNodeEditorViewModel CreateInspectorHost(IList<SceneNode> selection, Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider? assets = null, IMaterialPickerService? materials = null, Oxygen.Editor.ContentPipeline.Discovery.IBuiltinCatalogDiscovery? builtins = null, ISceneContentDemandService? contentDemand = null)
    {
        this.messenger.Register<SceneNodeSelectionRequestMessage>(this, (_, message) => message.Reply(selection));
        return new(this.hosting, new ViewModelToView(Mock.Of<IViewLocator>()), this.messenger, this.Commands, this.documents.Object, default, assets ?? this.AssetCatalog.Object, materials ?? this.MaterialPicker.Object, this.sync, builtins ?? new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), contentDemand ?? Mock.Of<ISceneContentDemandService>(), this.materialPipeline.Pipeline, this.projectContexts);
    }

    public async Task InitializeAsync(CancellationToken cancellationToken, bool mountPublished = false)
    {
        _ = (await this.engine.InitializeAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        this.engine.TargetFps = 60;
        await this.engine.StartAsync().ConfigureAwait(true);
        if (mountPublished)
        {
            await this.RefreshCookedRootsAsync().ConfigureAwait(true);
        }

        await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
    }

    public async Task<RuntimeEnvironmentState> ReadNativeAsync(CancellationToken cancellationToken)
    {
        var observedTarget = this.target ?? throw new InvalidOperationException("The scene must synchronize before native observation.");
        var observed = await this.engine.WorldCommands.ObserveEnvironmentAsync(Guid.NewGuid(), observedTarget, cancellationToken).ConfigureAwait(true);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.Exists.Should().BeTrue();
        _ = observed.State.AtmosphereExists.Should().BeTrue();
        _ = observed.State.PostProcessExists.Should().BeTrue();
        return observed.State;
    }

    public async Task SaveAndReopenAsync(CancellationToken cancellationToken)
    {
        _ = (await this.Commands.SaveSceneAsync(this.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = this.Context.Metadata.IsDirty.Should().BeFalse();
        this.Model.SetScene(value: null);
        this.sync.CloseDocument(this.Context.Metadata);
        this.Context.History.Clear();
        var reopened = await this.manager.LoadSceneAsync(this.Source).ConfigureAwait(true);
        _ = reopened.Should().NotBeNull();
        this.Source = reopened!;
        this.Context = new(this.Source.Id, new SceneDocumentMetadata(this.Source.Id), this.Source, UndoRedo.GetHistory(this.Source.Id));
        this.Model.SetScene(this.Source);
        await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(this.Source, this.Context.Metadata));
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(this.Source.RootNodes.ToList()));
    }

    public async Task<RuntimeNodeState> ReadNodeAsync(Guid nodeId, CancellationToken cancellationToken)
    {
        var observedTarget = this.target ?? throw new InvalidOperationException("The scene must synchronize before native observation.");
        var observed = await this.engine.WorldCommands.ObserveNodeAsync(Guid.NewGuid(), observedTarget, nodeId, cancellationToken).ConfigureAwait(true);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        return observed.State!;
    }

    public async ValueTask DisposeAsync()
    {
        this.Model.Dispose();
        await this.Model.PendingEdits.ConfigureAwait(true);
        this.sync.CloseDocument(this.Context.Metadata);
        this.sync.Dispose();
        this.Context.History.Clear();
        this.materialChoices.Dispose();
        try
        {
            await this.engine.DisposeAsync().ConfigureAwait(true);
        }
        finally
        {
            this.materialPipeline.Dispose();
            this.materialCookCoordinator.Dispose();
            foreach (var foreign in this.foreignMaterialPipelines.Values)
            {
                foreign.Pipeline.Dispose();
                foreign.Coordinator.Dispose();
            }

            this.compatibility.Dispose();
            this.directory.Delete(recursive: true);
        }
    }

    private async Task SynchronizeAsync(CancellationToken cancellationToken)
    {
        this.target = null;
        _ = this.sync.RegisterDocument(this.Source, this.Context.Metadata).Should().BeTrue();
        _ = (await this.sync.SyncSceneWhenReadyAsync(this.Source, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        _ = this.target.Should().NotBeNull();
    }

    public async Task SwitchToNewSceneAsync(int cascades, CancellationToken cancellationToken)
    {
        this.Model.SetScene(value: null);
        this.sync.CloseDocument(this.Context.Metadata);
        this.Context.History.Clear();
        var project = this.Source.Project;
        this.Source = Scene.CreateAndHydrate(project, new SceneData { Id = Guid.NewGuid(), Name = "Next scene" });
        SeedShadowTransitionScene(this.Source, cascades);
        project.Scenes.Add(this.Source);
        this.Context = new(this.Source.Id, new SceneDocumentMetadata(this.Source.Id), this.Source, UndoRedo.GetHistory(this.Source.Id));
        this.Model.SetScene(this.Source);
        await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
    }

    public EngineService Runtime => this.engine;

    public async Task<SceneEditorViewModel> CreateSceneEditorAsync(IContainer container)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var preferences = new Oxygen.Editor.World.Workspace.PreviewSettingsService(this.engine, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), this.projectContexts, publisher.Object, new OperationStatusReducer());
        await preferences.RestoreAsync(this.projectContexts.ActiveProject!).ConfigureAwait(true);
        return new(this.Context.Metadata, this.documents.Object, default, this.engine, this.sync, Mock.Of<IDocumentInputCommitter>(), publisher.Object, new OperationStatusReducer(), this.Commands, Mock.Of<IContentPipelineService>(), container, this.messenger, new SceneCookInputRegistrar(new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(), this.manager, this.hosting, this.documents.Object), preferences);
    }

    public IDisposable RegisterWorkspacePublication(CatalogWorkloadServices services, IProjectAssetCatalog catalog)
    {
        var project = services.Projects.ActiveProject!;
        return services.Publication.RegisterPreview(project, () => this.CreateWorkspacePreviewAsync(services, catalog, project));
    }

    public Task<ICookPublicationPreview?> CreateWorkspacePreviewAsync(CatalogWorkloadServices services, IProjectAssetCatalog catalog, Oxygen.Editor.Projects.ProjectContext project) => this.hosting.Dispatcher.DispatchAsync(() => Task.FromResult<ICookPublicationPreview?>(new WorkspacePublicationPreview(project, this.engine, this.hosting, catalog, this.messenger, () => ReferenceEquals(project, services.Projects.ActiveProject), static () =>
    {
    })));
}
