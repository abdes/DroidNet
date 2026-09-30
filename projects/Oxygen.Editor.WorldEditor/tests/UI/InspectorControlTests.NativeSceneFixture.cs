// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Owns a real Runtime scene and disk source for per-field control compatibility.</summary>
public sealed partial class InspectorControlTests
{
    private sealed partial class NativeSceneFixture : IAsyncDisposable
    {
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
            this.hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new DispatcherQueueScheduler(dispatcher), IsRunning = true };
            var publisher = new Mock<IOperationResultPublisher>();
            _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
            var results = publisher.Object;
            var settings = new Mock<DroidNet.Config.ISettingsService<IEngineSettings>>();
            _ = settings.SetupGet(value => value.Settings).Returns(engineSettings ?? new EngineSettings());
            this.engine = new EngineService(this.hosting, results, loggerFactory, engineSettings: settings.Object, nativeCompatibility: this.compatibility);
            this.sync = new SceneEngineSync(this.engine, operationResults: results, hostingContext: this.hosting);
            var project = new Project(new ProjectInfo("Environment fields", Category.Games, this.directory.FullName, "preview.png")
            {
                AuthoringMounts = [new("Content", "Content")],
            }) { Name = "Environment fields" };
            File.WriteAllText(Path.Combine(this.directory.FullName, "Project.oxy"), ProjectInfo.ToJson(project.ProjectInfo));
            this.projectContexts.Activate(ProjectContext.FromProjectInfo(project.ProjectInfo));
            this.materialCookCoordinator = new(this.projectContexts, NullLogger<ContentCookCoordinator>.Instance);
            this.materialPipeline = new(this.projectContexts, this.materialCookCoordinator, new CookDocumentRegistry());
            var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
            this.Source = Scene.CreateAndHydrate(project, new SceneData
            {
                Id = Guid.NewGuid(),
                Name = "Environment",
                Environment = new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } },
            });
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
            this.Commands = new SceneDocumentCommandService(
                Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
                Mock.Of<ISceneExplorerService>(),
                new SceneSelectionService(),
                this.sync,
                this.manager,
                this.documents.Object,
                default,
                this.messenger,
                results,
                new OperationStatusReducer(), this.materialPipeline.Pipeline, this.projectContexts);
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
            var info = await this.manager.LoadProjectInfoAsync(projectRoot).ConfigureAwait(true)
                ?? throw new InvalidOperationException("The native fixture project has not been saved.");
            using var publication = await this.MaterialPipelineFor(projectRoot).Publication.AcquireReadAsync(ProjectContext.FromProjectInfo(info), token).ConfigureAwait(true);
            return publication.FindProjectRoot("Content") ?? throw new InvalidOperationException("The fixture has not cooked its Content mount.");
        }

        public async Task RefreshCookedRootsAsync(bool mountPublished = true)
        {
            using var selected = await this.materialPipeline.Publication.AcquireReadAsync(this.projectContexts.ActiveProject!, CancellationToken.None).ConfigureAwait(true);
            var bindings = mountPublished ? selected.Roots.Zip(selected.RootPaths)
                .Select(static entry => new RuntimeCookedRoot(entry.Second, entry.First.Owner == Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project ? entry.First.Name : null)).ToArray() : [];
            await this.engine.RefreshProjectCookedRootsAsync(bindings, selected.Retain()).ConfigureAwait(true);
        }

        public Task SuspendCookedContentAsync() => this.engine.SuspendCookedContentAsync();

        public SceneNodeEditorViewModel CreateInspectorHost(
            IList<SceneNode> selection,
            Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider? assets = null,
            IMaterialPickerService? materials = null,
            Oxygen.Editor.ContentPipeline.Discovery.IBuiltinCatalogDiscovery? builtins = null,
            ISceneContentDemandService? contentDemand = null)
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
    }
}
