// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.IO.Abstractions;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Cooking;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.TestSupport;

internal sealed class NativeSceneFixture : IAsyncDisposable
{
    // The disk: the native cook reads and writes the project there.
    private static readonly RealFileSystem Disk = new();

    private readonly IDirectoryInfo directory = Disk.Directory.CreateTempSubdirectory("OxygenEnvironmentField-");
    private readonly ProjectManagerService manager = new(new NativeStorageProvider(Disk));
    private readonly bool ownsEngine;
    private readonly Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts? compatibility;
    private readonly IDisposable? sharedResults;
    private readonly IEngineService previewEngine;
    private readonly EngineProbe probe;
    private readonly IOperationResultPublisher previewResults;
    private readonly ContentCookCoordinator materialCookCoordinator;
    private readonly Oxygen.Editor.ContentPipeline.Testing.NativeContentPipelineFixture materialPipeline;
    private readonly Dictionary<FileSystemPath, (ContentCookCoordinator Coordinator, Oxygen.Editor.ContentPipeline.Testing.NativeContentPipelineFixture Pipeline)> foreignMaterialPipelines = [];
    private readonly TestDocumentArea area = new();
    private readonly MaterialValidation materialValidation = new();
    private readonly MaterialDocumentKind materialKind;
    private readonly StrongReferenceMessenger messenger = new();
    private readonly HostingContext hosting;
    private readonly BehaviorSubject<IReadOnlyList<Oxygen.Editor.Assets.AssetEntry>> feedAssets = new([]);
    private readonly SceneDocumentKind sceneKind;
    private IScenePreview? preview;
    private RuntimeSceneTarget? target;
    private PreviewSession? contentSession;

    /// <summary>Initializes a new instance of the <see cref="NativeSceneFixture"/> class.</summary>
    /// <param name="automatic">Whether the scene uses automatic exposure.</param>
    /// <param name="seed">Authors the initial scene.</param>
    /// <param name="engineSettings">
    ///     Startup settings that differ from the shared engine's. Supplying them, or a logger factory,
    ///     gives the fixture its own engine and stops the shared one for the duration of the test.
    /// </param>
    /// <param name="loggerFactory">Receives native engine logs; implies a fixture-owned engine.</param>
    /// <param name="layout">Authors the initial scene's explorer layout, from the seeded scene.</param>
    public NativeSceneFixture(bool automatic, Action<Scene>? seed = null, EngineStartupSettings? engineSettings = null, ILoggerFactory? loggerFactory = null, Func<Scene, IList<ExplorerEntryData>>? layout = null)
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
            var settings = new Mock<DroidNet.Config.ISettingsService<IEngineStartupSettings>>();
            _ = settings.SetupGet(value => value.Settings).Returns(engineSettings ?? new EngineStartupSettings());
            this.compatibility = Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            this.Runtime = new EngineService(this.hosting, results, loggerFactory, engineSettings: settings.Object, nativeCompatibility: this.compatibility);
        }
        else
        {
            this.Runtime = SharedNativeEngine.Engine;
            this.sharedResults = SharedNativeEngine.RouteResults(this.Results);
        }

        (this.previewEngine, this.probe) = EngineProbe.Of(this.Runtime);
        var project = TestProject.Create(Disk, this.directory.FullName).Context with { Name = "Environment fields", Thumbnail = "preview.png" };
        _ = TestProject.SaveProjectFile(project);
        this.Sessions = new TestSessionHost();
        var session = this.Sessions.Open(project);
        this.materialCookCoordinator = new(session, NullLogger<ContentCookCoordinator>.Instance);
        this.materialPipeline = new(session, this.materialCookCoordinator);
        var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
        this.Source = Scene.CreateAndHydrate(new SceneData { Id = SceneId.New(), Name = "Environment", Environment = new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } }, });
        seed?.Invoke(this.Source);
        _ = this.AssetCatalog.Setup(value => value.Items).Returns(this.feedAssets);
        _ = this.AssetCatalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);

        // The scene is a document of the project, saved where the editor saves scenes.
        this.sceneKind = new SceneDocumentKind(session);
        this.materialKind = new MaterialDocumentKind(this.materialValidation);
        this.OpenDocuments = new OpenDocuments([this.sceneKind, this.materialKind], new ImmediateReadFileStore(new NativeAtomicFileStore(Disk)));
        this.UnsavedDocuments = new UnsavedDocuments(this.OpenDocuments);
        this.previewResults = results;
        this.OpenScene(this.WriteScene(this.Source, layout?.Invoke(this.Source)));
    }

    /// <summary>Gets the file system that holds the project: the disk, which the native cook reads and writes.</summary>
    public static IFileSystem FileSystem => Disk;

    /// <summary>Gets the document area of the window that shows the transition host, once it was created.</summary>
    public DocumentArea? TransitionArea { get; private set; }

    public DocumentService? TransitionDocuments { get; private set; }

    public Scene Source { get; private set; }

    /// <summary>Gets what edits the document's scene.</summary>
    public SceneAuthoring Authoring => ((SceneContent)this.Document.Content).Authoring;

    /// <summary>Gets the explorer layout of the document's scene.</summary>
    public IList<ExplorerEntryData> Layout => this.Authoring.Layout;

    /// <summary>Gets the scene's document.</summary>
    public Oxygen.Editor.Documents.Document Document { get; private set; }

    /// <summary>Gets the tab of the scene's document.</summary>
    public DocumentTab Tab { get; private set; }

    /// <summary>Gets the open documents of the fixture's project session: its scene and the materials a test opens.</summary>
    public OpenDocuments OpenDocuments { get; }

    /// <summary>Gets the unsaved documents of <see cref="OpenDocuments"/>, as the editor's session reports them.</summary>
    public UnsavedDocuments UnsavedDocuments { get; }

    public Mock<Oxygen.Editor.Assets.IAssetFeed> AssetCatalog { get; } = new();

    public ConcurrentQueue<OperationResult> Results { get; } = new();

    public FileSystemPath ProjectRoot => FileSystemPath.From(this.directory.FullName);

    /// <summary>Gets the session host of the fixture's project, whose session its cook and preview serve.</summary>
    public TestSessionHost Sessions { get; }

    /// <summary>Gets the session of the fixture's project.</summary>
    public TestSession Session => this.Session;

    public EngineService Runtime { get; }

    private IScenePreview Preview => this.preview ?? throw new InvalidOperationException("The fixture shows its scene once it initializes.");

    public CookingPanelViewModel CreateTimingCookingPanel(CatalogWorkloadServices services) => new(services.Runs, services.Pipeline, services.Session, Mock.Of<ICookRecovery>(), this.hosting);

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
        WriteSceneFile(this.Document.File.Path, new SceneFileData(this.Source.Dehydrate(), this.Layout));
        _ = (await this.Document.CheckFileAsync(cancellationToken).ConfigureAwait(true)).Should().Be(DocumentFileState.Reloaded);
        await this.AdoptContentAsync(cancellationToken).ConfigureAwait(true);
    }

    /// <summary>Has another program save the scene with the legacy aerial start, and reloads it.</summary>
    /// <returns>The task that completes once the reloaded scene is shown.</returns>
    public async Task SetSavedLegacyAerialStartAsync()
    {
        var data = this.Source.Dehydrate();
        var environment = data.Environment;
        WriteSceneFile(this.Document.File.Path, new SceneFileData(data with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = -1 } } }, this.Layout));
        var reload = await this.Document.ReloadAsync().ConfigureAwait(true);
        _ = reload.Succeeded.Should().BeTrue(reload.Problem);
        await this.AdoptContentAsync(CancellationToken.None).ConfigureAwait(true);
    }

    public async Task<(AssetPath Asset, string Key)> CookTestMaterialAsync(string name, CancellationToken cancellationToken, System.Numerics.Vector4? baseColor = null, FileSystemPath? projectRoot = null)
    {
        var root = projectRoot ?? this.ProjectRoot;
        var path = root.Combine("Content").Combine("Materials").Combine($"{name}.omat.json");
        _ = Disk.Directory.CreateDirectory(path.Parent!.FullPath);
        var color = baseColor ?? System.Numerics.Vector4.One;
        var factor = System.Text.Json.JsonSerializer.Serialize(new[] { color.X, color.Y, color.Z, color.W });
        var source = $$"""
                { "name": "{{name}}",
                  "parameters": { "base_color": {{factor}}, "metalness": 0, "roughness": 0.5 } }
                """;
        await Disk.File.WriteAllTextAsync(path.FullPath, source, cancellationToken).ConfigureAwait(true);
        var native = this.MaterialPipelineFor(root);
        var asset = AssetPath.Parse($"/Content/Materials/{name}.omat");
        var result = await native.Pipeline.CookAssetAsync(asset, cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        var cookedRoot = await this.GetCookedRootAsync(root, cancellationToken).ConfigureAwait(true);
        var indexStream = Disk.File.OpenRead(cookedRoot.Combine("container.index.bin").FullPath);
        await using var indexLifetime = indexStream.ConfigureAwait(true);
        var cooked = LooseCookedIndex.Read(indexStream).Assets.Single(value => string.Equals(value.VirtualPath, asset.VirtualPath, StringComparison.Ordinal));
        if (projectRoot is null)
        {
            var row = new Oxygen.Editor.Assets.AssetEntry(asset, name, Oxygen.Editor.Assets.AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Mounted, asset.VirtualPath, SourcePath: null, path, cookedRoot.Combine("Materials").Combine($"{name}.omat"), AssetGuid: null, [], IsSelectable: true);
            this.feedAssets.OnNext([.. this.feedAssets.Value, row]);
        }

        var keyBytes = new byte[16];
        cooked.AssetKey.WriteBytes(keyBytes);
        return (asset, new Guid(keyBytes, bigEndian: true).ToString());
    }

    /// <summary>Changes the project's content mounts through the project's preview session, as the workspace does.</summary>
    /// <param name="project">The project with the mounts and order to apply.</param>
    /// <param name="cancellationToken">Cancels the change.</param>
    /// <returns>The task that completes once the change is saved, mounted and active.</returns>
    public async Task ApplyContentPriorityAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var desired = TestProject.InfoOf(project);
        if (this.contentSession is null)
        {
            var catalog = new Mock<IAssetPublicationTarget>();
            _ = catalog.Setup(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
            this.contentSession = this.CreatePreviewSession(this.materialPipeline.Publication, this.materialCookCoordinator, this.Session, catalog.Object);
            _ = (await this.contentSession.StartAsync(this.Session.Context, cancellationToken).ConfigureAwait(true)).Should().BeTrue("{0}", this.contentSession.Failure);
        }

        _ = (await this.contentSession.ChangeMountsAsync(this.Session.Context, desired, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
    }

    public DocumentAreaViewModel CreateDocumentTransitionHost(IContainer container, DocumentTabs documents, CatalogWorkloadServices services, WindowId windowId)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var appearance = new Mock<ISettingsService<IAppearanceSettings>>();
        _ = appearance.SetupGet(value => value.Settings).Returns(new AppearanceSettings());
        container.RegisterInstance(appearance.Object);
        container.RegisterInstance<IMessenger>(this.messenger);
        container.RegisterInstance(this.Preview);
        container.RegisterInstance(Mock.Of<IDocumentInputCommitter>());
        container.RegisterInstance<IContentPipelineService>(services.Pipeline);
        container.RegisterInstance<IProjectSession>(services.Session);
        container.RegisterInstance<IAssetFeed>(this.AssetCatalog.Object);
        var preferences = new PreviewSettingsService(this.Runtime, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), this.Session, publisher.Object, new OperationStatusReducer());
        container.RegisterInstance(preferences);
        var materials = new MaterialEdits(this.materialValidation, publisher.Object);
        var cook = new MaterialCookService(services.Pipeline, this.Session, NullLogger<MaterialCookService>.Instance);
        DocumentEditorDescriptor[] editors =
        [
            new(this.sceneKind, tab => new SceneEditorViewModel(tab, this.TransitionArea!, this.Runtime, this.Preview, Mock.Of<IDocumentInputCommitter>(), publisher.Object, new OperationStatusReducer(), services.Pipeline, container, Mock.Of<IAssetReveal>(), preferences)) { IsClosable = false, IsOnePerArea = true },
            new(this.materialKind, tab => new MaterialEditorViewModel(tab, materials, this.AssetCatalog.Object, this.hosting.DispatcherScheduler, cook, services.Session)),
        ];
        this.TransitionArea = new DocumentArea(documents, this.OpenDocuments, editors, windowId, static _ => { });
        container.RegisterInstance<IDocumentArea>(this.TransitionArea);
        this.TransitionDocuments = new DocumentService(
            this.TransitionArea,
            documents,
            this.OpenDocuments,
            new ProjectDocumentFiles(this.Session),
            Mock.Of<IDocumentClosePrompt>(),
            Mock.Of<IDocumentConfirmations>(),
            Mock.Of<IDocumentInputCommitter>(),
            publisher.Object);
        container.RegisterInstance<IDocumentService>(this.TransitionDocuments);
        var views = new Mock<IViewLocator>();
        _ = views.Setup(value => value.ResolveView(It.IsAny<object>())).Returns((object model) => model is SceneEditorViewModel ? new SceneEditorView() : new MaterialEditorView());
        return new DocumentAreaViewModel(this.TransitionDocuments, this.TransitionArea, Mock.Of<IDocumentConfirmations>(), windowId, views.Object);
    }

    /// <summary>Waits until the engine shows the transition's scene document, which builds its editor's panes.</summary>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>The task that completes once the scene is shown.</returns>
    public Task NotifyTransitionSceneReadyAsync(CancellationToken cancellationToken) => this.SynchronizeAsync(stale: null, cancellationToken);

    /// <summary>Starts the preview cooks that follow what the open scene documents reference, as the workspace does.</summary>
    /// <param name="services">The project's cook services.</param>
    /// <param name="provider">The asset feed the demand resolves references with.</param>
    /// <returns>The demand; disposing it cancels its cooks.</returns>
    public IDisposable CreateImportedAssetDemand(CatalogWorkloadServices services, AssetFeed provider)
        => ContentDemands.Start(this.OpenDocuments, provider, services.Pipeline, services.Session);

    public async Task<MaterialSlotTarget> ReadSingleMaterialSlotAsync(NodeId nodeId, CancellationToken cancellationToken)
    {
        var node = this.Source.RootNodes.Select(root => SceneTraversal.FindNodeById(root, nodeId)).OfType<SceneNode>().Single();
        var uri = node.Components.OfType<GeometryComponent>().Single().Geometry;
        var inventory = await this.materialPipeline.Pipeline.ReadAsync(this.Session.Context, uri, cancellationToken).ConfigureAwait(true);
        _ = inventory.Should().NotBeNull("the native fixture geometry must have a current slot inventory");
        var slot = inventory!.Slots.Should().ContainSingle().Which;
        return new(uri, slot.SlotId, inventory.LayoutRevision);
    }

    public async Task<FileSystemPath> GetCookedRootAsync(FileSystemPath projectRoot, CancellationToken token)
    {
        var info = await this.manager.LoadProjectInfoAsync(projectRoot.FullPath).ConfigureAwait(true) ?? throw new InvalidOperationException("The native fixture project has not been saved.");
        using var publication = await this.MaterialPipelineFor(projectRoot).Publication.AcquireReadAsync(TestProject.Open(info, Disk).Context, token).ConfigureAwait(true);
        return publication.FindProjectRoot("Content") ?? throw new InvalidOperationException("The fixture has not cooked its Content mount.");
    }

    public async Task RefreshCookedRootsAsync(bool mountPublished = true)
    {
        using var selected = await this.materialPipeline.Publication.AcquireReadAsync(this.Session.Context, CancellationToken.None).ConfigureAwait(true);
        var bindings = mountPublished ? selected.Roots.Zip(selected.RootPaths).Select(static entry => new RuntimeCookedRoot(entry.Second, RuntimeCookedRootKind.LooseFolder)).ToArray() : [];
        await this.Runtime.RefreshProjectCookedRootsAsync(bindings, selected.Retain()).ConfigureAwait(true);
    }

    public Task SuspendCookedContentAsync() => this.Runtime.SuspendCookedContentAsync();

    /// <summary>Opens the fixture's scene on a running engine, starting the shared engine only if it is not running.</summary>
    /// <param name="cancellationToken">Cancels startup and synchronization.</param>
    /// <param name="mountPublished">Whether the project's published content, rather than none, is mounted before synchronization.</param>
    /// <returns>The initialization task.</returns>
    public async Task InitializeAsync(CancellationToken cancellationToken, bool mountPublished = false)
    {
        if (this.ownsEngine)
        {
            await SharedNativeEngine.StandDownAsync().ConfigureAwait(true);
            _ = (await this.Runtime.InitializeAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
            this.Runtime.TargetFps = 60;
            await this.Runtime.StartAsync().ConfigureAwait(true);
        }
        else
        {
            _ = (await SharedNativeEngine.EnsureRunningAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        }

        // The workspace mounts the project's content before it shows a scene, and the engine holds a scene's asset
        // loads until it does; without published content the mount is empty.
        if (mountPublished)
        {
            await this.RefreshCookedRootsAsync().ConfigureAwait(true);
        }
        else
        {
            await this.Runtime.RefreshProjectCookedRootsAsync([]).ConfigureAwait(true);
        }

        // One preview shows a scene on an engine, as in a workspace: the fixture's starts once it initializes.
        this.preview ??= ScenePreviews.Create(this.previewEngine, this.OpenDocuments, operationResults: this.previewResults);

        await this.SynchronizeAsync(stale: null, cancellationToken).ConfigureAwait(true);
    }

    public async Task<RuntimeEnvironmentState> ReadNativeAsync(CancellationToken cancellationToken)
    {
        var observedTarget = this.target ?? throw new InvalidOperationException("The scene must synchronize before native observation.");
        var observed = await this.Runtime.WorldCommands.ObserveEnvironmentAsync(Guid.NewGuid(), observedTarget, cancellationToken).ConfigureAwait(true);
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
        var stale = this.target;
        this.CloseScene();
        this.OpenScene(file);
        await this.SynchronizeAsync(stale, cancellationToken).ConfigureAwait(true);
    }

    public async Task<RuntimeNodeState> ReadNodeAsync(NodeId nodeId, CancellationToken cancellationToken)
    {
        var observedTarget = this.target ?? throw new InvalidOperationException("The scene must synchronize before native observation.");
        var observed = await this.Runtime.WorldCommands.ObserveNodeAsync(Guid.NewGuid(), observedTarget, nodeId, cancellationToken).ConfigureAwait(true);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        return observed.State!;
    }

    public async Task SwitchToNewSceneAsync(int cascades, CancellationToken cancellationToken)
    {
        var stale = this.target;
        this.CloseScene();
        var next = Scene.CreateAndHydrate(new SceneData { Id = SceneId.New(), Name = "Next scene" });
        SeedShadowTransitionScene(next, cascades);
        this.OpenScene(this.WriteScene(next));
        await this.SynchronizeAsync(stale, cancellationToken).ConfigureAwait(true);
    }

    public async Task<SceneEditorViewModel> CreateSceneEditorAsync(IContainer container)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        var preferences = new PreviewSettingsService(this.Runtime, Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(), this.Session, publisher.Object, new OperationStatusReducer());
        await preferences.EnsureRestoredAsync().ConfigureAwait(true);
        return new(this.Tab, this.area, this.Runtime, this.Preview, Mock.Of<IDocumentInputCommitter>(), publisher.Object, new OperationStatusReducer(), Mock.Of<IContentPipelineService>(), container, Mock.Of<IAssetReveal>(), preferences);
    }

    /// <summary>Starts the project's preview session over the cook services, as the workspace does.</summary>
    /// <param name="services">The project's cook services.</param>
    /// <param name="catalog">The asset catalog that shows the mounted publication.</param>
    /// <returns>The started session; disposing it releases the project's cooked content.</returns>
    public async Task<PreviewSession> StartWorkspacePreviewAsync(CatalogWorkloadServices services, IAssetPublicationTarget catalog)
    {
        var session = this.CreatePreviewSession(services.Publication, services.Runs, services.Session, catalog);
        _ = (await session.StartAsync(services.Session.Context).ConfigureAwait(true)).Should().BeTrue("{0}", session.Failure);
        return session;
    }

    public async ValueTask DisposeAsync()
    {
        if (this.contentSession is not null)
        {
            await this.contentSession.DisposeAsync().ConfigureAwait(true);
        }

        this.UnsavedDocuments.Dispose();
        this.OpenDocuments.Dispose();
        (this.preview as IDisposable)?.Dispose();
        this.feedAssets.Dispose();
        try
        {
            if (this.ownsEngine)
            {
                await this.Runtime.DisposeAsync().ConfigureAwait(true);
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
            this.Sessions.Dispose();
            foreach (var (coordinator, pipeline) in this.foreignMaterialPipelines.Values)
            {
                pipeline.Dispose();
                coordinator.Dispose();
            }

            this.compatibility?.Dispose();
            this.directory.Delete(recursive: true);
        }
    }

    private static void WriteSceneFile(FileSystemPath path, SceneFileData data)
    {
        _ = Disk.Directory.CreateDirectory(path.Parent!.FullPath);
        Disk.File.WriteAllBytes(path.FullPath, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(data, SceneFileJsonContext.Default.SceneFileData));
    }

    private Oxygen.Editor.ContentPipeline.Testing.NativeContentPipelineFixture MaterialPipelineFor(FileSystemPath root)
    {
        if (root == this.ProjectRoot)
        {
            return this.materialPipeline;
        }

        if (!this.foreignMaterialPipelines.TryGetValue(root, out var value))
        {
            var project = TestProject.Create(Disk, root.FullPath).Context with { Name = "Foreign material fixture" };
            _ = TestProject.SaveProjectFile(project);
            var session = new TestSession(project);
            var coordinator = new ContentCookCoordinator(session, NullLogger<ContentCookCoordinator>.Instance);
            value = (coordinator, new(session, coordinator));
            this.foreignMaterialPipelines.Add(root, value);
        }

        return value.Pipeline;
    }

    // Waits until the engine shows the current document's scene in an activation other than the stale one.
    private async Task SynchronizeAsync(RuntimeSceneTarget? stale, CancellationToken cancellationToken)
    {
        this.target = null;
        var document = this.Document.Id;
        var scene = this.Source.Id;
        using var changed = new SemaphoreSlim(0);
        void OnReadyChanged(object? sender, ScenePreviewEventArgs args) => changed.Release();
        void OnActivated(object? sender, EventArgs args) => changed.Release();
        this.Preview.ReadyChanged += OnReadyChanged;
        this.probe.Activated += OnActivated;
        try
        {
            while (!(this.Preview.IsReady(document) && this.probe.LastActivated is { } shown && shown.SceneId == scene && shown != stale))
            {
                await changed.WaitAsync(cancellationToken).ConfigureAwait(true);
            }

            this.target = this.probe.LastActivated;
        }
        finally
        {
            this.Preview.ReadyChanged -= OnReadyChanged;
            this.probe.Activated -= OnActivated;
        }
    }

    // Saves a scene built in memory as a scene file of the project, named after the scene.
    private DocumentFile WriteScene(Scene scene, IList<ExplorerEntryData>? layout = null)
    {
        var file = new DocumentFile(this.ProjectRoot.Combine("Content").Combine("Scenes").Combine($"{scene.Name}.oscene.json"), AssetPath.Create("Content", $"Scenes/{scene.Name}.oscene"));
        WriteSceneFile(file.Path, new SceneFileData(scene.Dehydrate(), layout));
        return file;
    }

    // Opens the scene's file as its document; the file store reads at once, so this completes before it returns.
    [System.Diagnostics.CodeAnalysis.MemberNotNull(nameof(Document), nameof(Tab), nameof(Source))]
    private void OpenScene(DocumentFile file)
    {
        var opening = this.OpenDocuments.OpenAsync(file);
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
        _ = this.area.CloseAsync(this.Document);
        this.OpenDocuments.Close(this.Document);
    }

    // A reload replaced the document's scene: show it, and have the engine show it once it showed the previous one.
    private async Task AdoptContentAsync(CancellationToken cancellationToken)
    {
        this.Source = ((SceneContent)this.Document.Content).Scene;
        if (this.target is { } stale)
        {
            await this.SynchronizeAsync(stale, cancellationToken).ConfigureAwait(true);
        }
    }

    private async Task FollowRelocationAsync(Oxygen.Editor.ContentPipeline.Relocation.AssetRelocationChange change)
    {
        if (DocumentRelocations.Map(this.Document, change, this.Session.Context, out _) is { } relocation)
        {
            await this.Document.FollowRelocationAsync(relocation).ConfigureAwait(true);
        }
    }

    // The session follows the fixture's scene project, whose loaded mounts an accepted mount change updates.
    private PreviewSession CreatePreviewSession(CookPublicationService publication, IContentCookCoordinator coordinator, IProjectSession session, IAssetPublicationTarget catalog)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
        return new PreviewSession(this.previewEngine, publication, new CookedContentMountService(), catalog, coordinator, session, this.Sessions, publisher.Object, new OperationStatusReducer());
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
            => Task.FromResult(SnapshotOf(path));

        public Task<DroidNet.Storage.FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, DroidNet.Storage.FileVersion expected, CancellationToken cancellationToken = default)
            => writer.WriteAsync(path, content, expected, cancellationToken);

        // Reads before returning, so the open that awaits it completes at once.
        private static DroidNet.Storage.FileSnapshot SnapshotOf(string path)
        {
            if (!Disk.File.Exists(path))
            {
                return new DroidNet.Storage.FileSnapshot([], DroidNet.Storage.FileVersion.Missing);
            }

            var bytes = Disk.File.ReadAllBytes(path);
            return new DroidNet.Storage.FileSnapshot([.. bytes], new DroidNet.Storage.FileVersion(Exists: true, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes))));
        }
    }
}
