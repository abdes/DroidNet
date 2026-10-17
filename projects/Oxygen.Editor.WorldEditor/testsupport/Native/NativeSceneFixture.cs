// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.World.Workspace;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class NativeSceneFixture : IAsyncDisposable
{
    public CookingPanelViewModel CreateTimingCookingPanel(CatalogWorkloadServices services) => new(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), this.hosting);

    public IDisposable RegisterMainCookDocument(CatalogWorkloadServices services)
        => new OpenDocumentCookInputs(this.sceneDocuments, services.Documents);

    /// <summary>Has the open scene follow the relocations a service commits, as its scene editor would.</summary>
    /// <param name="relocation">The relocation service.</param>
    /// <returns>The registration to dispose when the scene stops following.</returns>
    public IDisposable FollowRelocations(Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService relocation)
        => relocation.AddParticipant(new RelocationFollower(this));

    /// <summary>Saves a scene a test changed in memory, outside the commands, and shows it as reloaded from its file.</summary>
    /// <param name="cancellationToken">Cancels the reload and the engine's synchronization.</param>
    /// <returns>The task that completes once the saved scene is shown.</returns>
    public async Task SaveSceneChangedInMemoryAsync(CancellationToken cancellationToken)
    {
        this.Model.SetScene(value: null);
        WriteSceneFile(this.Document.File.Path, this.Source.Dehydrate());
        _ = (await this.Document.CheckFileAsync(cancellationToken).ConfigureAwait(true)).Should().Be(DocumentFileState.Reloaded);
        await this.AdoptContentAsync(cancellationToken).ConfigureAwait(true);
    }

    /// <summary>Has another program save the scene with the legacy aerial start, and reloads it.</summary>
    /// <returns>The task that completes once the reloaded scene is shown.</returns>
    public async Task SetSavedLegacyAerialStartAsync()
    {
        this.Model.SetScene(value: null);
        var data = this.Source.Dehydrate();
        var environment = data.Environment;
        WriteSceneFile(this.Document.File.Path, data with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = -1 } } });
        var reload = await this.Document.ReloadAsync().ConfigureAwait(true);
        _ = reload.Succeeded.Should().BeTrue(reload.Problem);
        await this.AdoptContentAsync(CancellationToken.None).ConfigureAwait(true);
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
            var row = new Oxygen.Editor.Assets.AssetEntry(uri, name, Oxygen.Editor.Assets.AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Mounted, Oxygen.Managed.Assets.Catalog.AssetUriHelper.GetVirtualPath(uri), SourcePath: null, path, CookedUri: null, Path.Combine(cookedRoot, "Materials", $"{name}.omat"), AssetGuid: null, [], IsSelectable: true);
            this.feedAssets.OnNext([.. this.feedAssets.Value, row]);
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
        var catalog = new Mock<IAssetPublicationTarget>();
        _ = catalog.Setup(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var registration = this.materialPipeline.Publication.RegisterPreview(current, () => Task.FromResult<ICookPublicationPreview?>(new WorkspacePublicationPreview(current, this.engine, this.hosting, catalog.Object, () => ReferenceEquals(current, this.projectContexts.ActiveProject), static () =>
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
                container.RegisterInstance(Mock.Of<IDocumentInputCommitter>());
        container.RegisterInstance(Mock.Of<IDocumentConflictPrompt>());
        container.RegisterInstance<IContentPipelineService>(services.Pipeline);
        container.RegisterInstance<IProjectContextService>(services.Projects);
        container.RegisterInstance<IAssetFeed>(this.AssetCatalog.Object);
        var preferences = new Oxygen.Editor.World.Workspace.PreviewSettingsService(this.engine, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), services.Projects, publisher.Object, new OperationStatusReducer());
        container.RegisterInstance(preferences);
        var materials = new MaterialEdits(this.materialValidation, publisher.Object);
        var cook = new MaterialCookService(services.Pipeline, services.Projects, NullLogger<MaterialCookService>.Instance);
        DocumentEditorDescriptor[] editors =
        [
            new(this.sceneKind, tab => new SceneEditorViewModel(tab, this.TransitionArea!, windowId, this.engine, this.sync, Mock.Of<IDocumentInputCommitter>(), publisher.Object, new OperationStatusReducer(), services.Pipeline, container, this.messenger, preferences)) { IsClosable = false },
            new(this.materialKind, tab => new MaterialEditorViewModel(tab, materials, this.AssetCatalog.Object, this.hosting.DispatcherScheduler, cook, services.Projects, windowId: windowId)),
        ];
        this.TransitionArea = new DocumentArea(documents, this.sceneDocuments, editors, windowId, static _ => { });
        container.RegisterInstance<IDocumentArea>(this.TransitionArea);
        var views = new Mock<IViewLocator>();
        _ = views.Setup(value => value.ResolveView(It.IsAny<object>())).Returns((object model) => model is SceneEditorViewModel ? new SceneEditorView() : new MaterialEditorView());
        return new(documents, this.TransitionArea, this.sceneDocuments, views.Object, this.engine, new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), publisher.Object), Mock.Of<IWindowManagerService>(), windowId);
    }

    /// <summary>Gets the document area of the window that shows the transition host, once it was created.</summary>
    public DocumentArea? TransitionArea { get; private set; }

    public DocumentManager CreateTransitionDocumentManager(IProjectContextService projects, WindowId windowId)
        => new(this.TransitionArea!, this.messenger, projects, Mock.Of<Oxygen.Editor.Data.Services.IProjectUsageService>(), windowId, this.sceneDocuments, this.sync, Mock.Of<IAssetRelocationService>());

    public async Task NotifyTransitionSceneReadyAsync(CancellationToken cancellationToken)
    {
        void Publish(object? sender, SceneSynchronizationCompletedEventArgs args)
        {
            if (args.DocumentId == this.Document.Id)
            {
                _ = this.messenger.Send(new SceneLoadedMessage(args.Scene, args.DocumentId));
            }
        }

        this.sync.SceneSynchronized += Publish;
        try
        {
            _ = (await this.sync.SyncSceneWhenReadyAsync(this.Source, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        }
        finally
        {
            this.sync.SceneSynchronized -= Publish;
        }
    }


    public SceneContentDemandService CreateImportedAssetDemand(CatalogWorkloadServices services, AssetFeed provider)
    {
        var demand = new SceneContentDemandService(this.hosting, provider, services.Pipeline, services.Projects, this.area, this.sync, this.messenger, NullLogger<SceneContentDemandService>.Instance);
        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(this.Source, this.Document));
        return demand;
    }

    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenEnvironmentField-");
    private readonly ProjectManagerService manager = new(new NativeStorageProvider(new RealFileSystem()));
    private readonly EngineService engine;
    private readonly bool ownsEngine;
    private readonly Oxygen.Testing.TemporaryNativeArtifacts? compatibility;
    private readonly IDisposable? sharedResults;
    private readonly SceneEngineSync sync;
    private readonly ProjectContextService projectContexts = new();
    private readonly ContentCookCoordinator materialCookCoordinator;
    private readonly Oxygen.Testing.NativeContentPipelineFixture materialPipeline;
    private readonly Mock<IGeometryMaterialSlotProvider> materialSlotProvider = new();
    private readonly Dictionary<string, (ContentCookCoordinator Coordinator, Oxygen.Testing.NativeContentPipelineFixture Pipeline)> foreignMaterialPipelines = new(StringComparer.OrdinalIgnoreCase);
    private readonly TestDocumentArea area = new();
    private readonly MaterialValidation materialValidation = new();
    private readonly MaterialDocumentKind materialKind;
    private readonly StrongReferenceMessenger messenger = new();
    private readonly HostingContext hosting;
    private readonly BehaviorSubject<IReadOnlyList<Oxygen.Editor.Assets.AssetEntry>> feedAssets = new([]);
    private readonly OpenDocuments sceneDocuments;
    private readonly SceneDocumentKind sceneKind;
    private RuntimeSceneTarget? target;
    private IGeometryMaterialSlotProvider activeMaterialSlotProvider;

    /// <summary>Initializes a new instance of the <see cref="NativeSceneFixture"/> class.</summary>
    /// <param name="automatic">Whether the scene uses automatic exposure.</param>
    /// <param name="seed">Authors the initial scene.</param>
    /// <param name="engineSettings">
    ///     Startup settings that differ from the shared engine's. Supplying them, or a logger factory,
    ///     gives the fixture its own engine and stops the shared one for the duration of the test.
    /// </param>
    /// <param name="loggerFactory">Receives native engine logs; implies a fixture-owned engine.</param>
    public NativeSceneFixture(bool automatic, Action<Scene>? seed = null, EngineSettings? engineSettings = null, ILoggerFactory? loggerFactory = null)
    {
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        this.hosting = new HostingContext
        {
            Application = Application.Current,
            Dispatcher = dispatcher,
            DispatcherScheduler = new DispatcherQueueScheduler(dispatcher),
            IsRunning = true,
        };
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var results = publisher.Object;
        this.ownsEngine = engineSettings is not null || loggerFactory is not null;
        if (this.ownsEngine)
        {
            var settings = new Mock<DroidNet.Config.ISettingsService<IEngineSettings>>();
            _ = settings.SetupGet(value => value.Settings).Returns(engineSettings ?? new EngineSettings());
            this.compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            this.engine = new EngineService(this.hosting, results, loggerFactory, engineSettings: settings.Object, nativeCompatibility: this.compatibility);
        }
        else
        {
            this.engine = SharedNativeEngine.Engine;
            this.sharedResults = SharedNativeEngine.RouteResults(this.Results);
        }

        this.sync = new SceneEngineSync(this.engine, operationResults: results, hostingContext: this.hosting);
        var project = new Project(new ProjectInfo("Environment fields", Category.Games, this.directory.FullName, "preview.png") { AuthoringMounts = [new("Content", "Content")], })
        {
            Name = "Environment fields",
        };
        File.WriteAllText(Path.Combine(this.directory.FullName, "Project.oxy"), ProjectInfo.ToJson(project.ProjectInfo));
        this.projectContexts.Activate(ProjectContext.FromProjectInfo(project.ProjectInfo));
        this.materialCookCoordinator = new(this.projectContexts, NullLogger<ContentCookCoordinator>.Instance);
        this.materialPipeline = new(this.projectContexts, this.materialCookCoordinator, new CookDocumentRegistry());
        this.activeMaterialSlotProvider = this.materialPipeline.Pipeline;
        _ = this.materialSlotProvider.Setup(provider => provider.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<Uri>(), It.IsAny<CancellationToken>()))
            .Returns((ProjectContext context, Uri uri, CancellationToken token) => this.activeMaterialSlotProvider.ReadAsync(context, uri, token));
        var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
        this.Source = Scene.CreateAndHydrate(project, new SceneData { Id = SceneId.New(), Name = "Environment", Environment = new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } }, });
        seed?.Invoke(this.Source);
        _ = this.AssetCatalog.Setup(value => value.Items).Returns(this.feedAssets);
        _ = this.AssetCatalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);

        // The scene is a document of the project, saved where the editor saves scenes.
        this.sceneKind = new SceneDocumentKind(project);
        this.materialKind = new MaterialDocumentKind(this.materialValidation);
        this.sceneDocuments = new OpenDocuments([this.sceneKind, this.materialKind], new ImmediateReadFileStore(new NativeAtomicFileStore(new RealFileSystem())));
        this.OpenScene(this.WriteScene(this.Source));
        this.Model = new EnvironmentViewModel(() => this.Authoring);
        this.Model.SetScene(this.Source);
        this.sync.SceneSynchronized += (_, args) =>
        {
            if (args.DocumentId == this.Document.Id)
            {
                this.target = this.sync.GetSynchronizedTarget(args.DocumentId);
            }
        };
    }

    public Scene Source { get; private set; }

    /// <summary>Gets what edits the document's scene.</summary>
    public SceneAuthoring Authoring => ((SceneContent)this.Document.Content).Authoring;

    /// <summary>Gets the scene's document.</summary>
    public Oxygen.Editor.Documents.Document Document { get; private set; }

    /// <summary>Gets the tab of the scene's document.</summary>
    public DocumentTab Tab { get; private set; }

    /// <summary>Gets the open documents of the fixture's project session: its scene and the materials a test opens.</summary>
    public OpenDocuments OpenDocuments => this.sceneDocuments;

    public EnvironmentViewModel Model { get; }

    public Mock<Oxygen.Editor.Assets.IAssetFeed> AssetCatalog { get; } = new();

    public ConcurrentQueue<OperationResult> Results { get; } = new();

    public string ProjectRoot => this.directory.FullName;

    public ProjectContextService Projects => this.projectContexts;

    public async Task<MaterialSlotTarget> ReadSingleMaterialSlotAsync(NodeId nodeId, CancellationToken cancellationToken)
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

    public SceneNodeEditorViewModel CreateInspectorHost(IList<SceneNode> selection, Oxygen.Editor.Assets.IAssetFeed? assets = null, Oxygen.Editor.ContentPipeline.Discovery.IBuiltinCatalogDiscovery? builtins = null, ISceneContentDemandService? contentDemand = null, IGeometryMaterialSlotProvider? materialSlots = null)
    {
        this.activeMaterialSlotProvider = materialSlots ?? this.materialPipeline.Pipeline;
        this.messenger.Register<SceneNodeSelectionRequestMessage>(this, (_, message) => message.Reply(selection));
        return new(this.hosting, new ViewModelToView(Mock.Of<IViewLocator>()), this.messenger, this.area, default, assets ?? this.AssetCatalog.Object, this.sync, new SceneSelectionService(), builtins ?? new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), contentDemand ?? Mock.Of<ISceneContentDemandService>(), this.materialSlotProvider.Object, this.projectContexts);
    }

    /// <summary>Opens the fixture's scene on a running engine, starting the shared engine only if it is not running.</summary>
    /// <param name="cancellationToken">Cancels startup and synchronization.</param>
    /// <param name="mountPublished">Whether the project's published content is mounted before synchronization.</param>
    /// <returns>The initialization task.</returns>
    public async Task InitializeAsync(CancellationToken cancellationToken, bool mountPublished = false)
    {
        if (this.ownsEngine)
        {
            await SharedNativeEngine.StandDownAsync().ConfigureAwait(true);
            _ = (await this.engine.InitializeAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
            this.engine.TargetFps = 60;
            await this.engine.StartAsync().ConfigureAwait(true);
        }
        else
        {
            _ = (await SharedNativeEngine.EnsureRunningAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        }

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
        var saved = await this.Document.SaveAsync(cancellationToken).ConfigureAwait(true);
        _ = saved.Succeeded.Should().BeTrue(saved.Message);
        _ = this.Document.IsDirty.Should().BeFalse();
        var file = this.Document.File;
        this.CloseScene();
        this.OpenScene(file);
        this.Model.SetScene(this.Source);
        await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(this.Source, this.Document));
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage(this.Source.RootNodes.ToList()));
    }

    public async Task<RuntimeNodeState> ReadNodeAsync(NodeId nodeId, CancellationToken cancellationToken)
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
        this.sync.CloseDocument(this.Document.Id);
        this.sync.Dispose();
        this.sceneDocuments.Dispose();
        this.feedAssets.Dispose();
        try
        {
            if (this.ownsEngine)
            {
                await this.engine.DisposeAsync().ConfigureAwait(true);
            }
            else
            {
                this.sharedResults!.Dispose();
                await SharedNativeEngine.ResetAsync().ConfigureAwait(true);
            }
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

            this.compatibility?.Dispose();
            this.directory.Delete(recursive: true);
        }
    }

    private async Task SynchronizeAsync(CancellationToken cancellationToken)
    {
        this.target = null;
        _ = this.sync.RegisterDocument(this.Authoring).Should().BeTrue();
        _ = (await this.sync.SyncSceneWhenReadyAsync(this.Source, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        _ = this.target.Should().NotBeNull();
    }

    public async Task SwitchToNewSceneAsync(int cascades, CancellationToken cancellationToken)
    {
        var project = this.Source.Project;
        this.CloseScene();
        var next = Scene.CreateAndHydrate(project, new SceneData { Id = SceneId.New(), Name = "Next scene" });
        SeedShadowTransitionScene(next, cascades);
        this.OpenScene(this.WriteScene(next));
        this.Model.SetScene(this.Source);
        await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
    }

    private static void WriteSceneFile(string path, SceneData data)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData));
    }

    // Saves a scene built in memory as a scene file of the project, named after the scene.
    private DocumentFile WriteScene(Scene scene)
    {
        var relative = $"Content/Scenes/{scene.Name}.oscene.json";
        var file = new DocumentFile(Path.Combine(this.ProjectRoot, relative.Replace('/', Path.DirectorySeparatorChar)), new Uri("asset:///" + Uri.EscapeDataString(relative).Replace("%2F", "/", StringComparison.Ordinal)));
        WriteSceneFile(file.Path, scene.Dehydrate());
        return file;
    }

    // Opens the scene's file as its document; the file store reads at once, so this completes before it returns.
    [System.Diagnostics.CodeAnalysis.MemberNotNull(nameof(Document), nameof(Tab), nameof(Source))]
    private void OpenScene(DocumentFile file)
    {
        var opening = this.sceneDocuments.OpenAsync(file);
        if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { Content: SceneContent content } document)
        {
            throw new InvalidOperationException($"The fixture scene '{file.Path}' did not open: {(opening.IsCompletedSuccessfully ? opening.Result.Problem : "the open did not complete at once")}.");
        }

        this.Document = document;
        this.Tab?.Dispose();
        this.Tab = new DocumentTab(document, new DocumentEditorDescriptor(this.sceneKind, static _ => new object()) { IsClosable = false });
        this.area.Activate(document);
        this.Source = content.Scene;
    }

    private void CloseScene()
    {
        this.Model.SetScene(value: null);
        this.sync.CloseDocument(this.Document.Id);
        _ = this.area.CloseAsync(this.Document);
        this.sceneDocuments.Close(this.Document);
    }

    // A reload replaced the document's scene: show it, and have the engine show it once it showed the previous one.
    private async Task AdoptContentAsync(CancellationToken cancellationToken)
    {
        this.Source = ((SceneContent)this.Document.Content).Scene;
        this.Model.SetScene(this.Source);
        if (this.target is not null)
        {
            await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
        }
    }

    private async Task FollowRelocationAsync(Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationChange change)
    {
        if (DocumentRelocations.Map(this.Document, change, this.projectContexts.ActiveProject!, out _) is { } relocation)
        {
            await this.Document.FollowRelocationAsync(relocation).ConfigureAwait(true);
        }
    }

    /// <summary>Follows relocations on the fixture's UI thread; the relocation calls participants from a worker.</summary>
    private sealed class RelocationFollower(NativeSceneFixture owner) : Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationParticipant
    {
        public Task FollowAsync(Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationChange change)
            => owner.hosting.Dispatcher.DispatchAsync(() => owner.FollowRelocationAsync(change));
    }

    /// <summary>Reads files at once and writes them atomically, so a document can open in a constructor.</summary>
    private sealed class ImmediateReadFileStore(DroidNet.Storage.IAtomicFileStore writer) : DroidNet.Storage.IAtomicFileStore
    {
        public Task<DroidNet.Storage.FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
        {
            if (!File.Exists(path))
            {
                return Task.FromResult(new DroidNet.Storage.FileSnapshot([], DroidNet.Storage.FileVersion.Missing));
            }

            var bytes = File.ReadAllBytes(path);
            return Task.FromResult(new DroidNet.Storage.FileSnapshot([.. bytes], new DroidNet.Storage.FileVersion(Exists: true, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)))));
        }

        public Task<DroidNet.Storage.FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, DroidNet.Storage.FileVersion expected, CancellationToken cancellationToken = default)
            => writer.WriteAsync(path, content, expected, cancellationToken);
    }

    public EngineService Runtime => this.engine;

    public async Task<SceneEditorViewModel> CreateSceneEditorAsync(IContainer container)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var preferences = new Oxygen.Editor.World.Workspace.PreviewSettingsService(this.engine, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), this.projectContexts, publisher.Object, new OperationStatusReducer());
        await preferences.RestoreAsync(this.projectContexts.ActiveProject!).ConfigureAwait(true);
        return new(this.Tab, this.area, default, this.engine, this.sync, Mock.Of<IDocumentInputCommitter>(), publisher.Object, new OperationStatusReducer(), Mock.Of<IContentPipelineService>(), container, this.messenger, preferences);
    }

    public IDisposable RegisterWorkspacePublication(CatalogWorkloadServices services, IAssetPublicationTarget catalog)
    {
        var project = services.Projects.ActiveProject!;
        return services.Publication.RegisterPreview(project, () => this.CreateWorkspacePreviewAsync(services, catalog, project));
    }

    public Task<ICookPublicationPreview?> CreateWorkspacePreviewAsync(CatalogWorkloadServices services, IAssetPublicationTarget catalog, Oxygen.Editor.Projects.ProjectContext project) => this.hosting.Dispatcher.DispatchAsync(() => Task.FromResult<ICookPublicationPreview?>(new WorkspacePublicationPreview(project, this.engine, this.hosting, catalog, () => ReferenceEquals(project, services.Projects.ActiveProject), static () =>
    {
    })));
}
