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
        private readonly Mock<IDocumentService> documents = new();
        private readonly StrongReferenceMessenger messenger = new();
        private readonly HostingContext hosting;
        private readonly BehaviorSubject<IReadOnlyList<MaterialPickerResult>> materialChoices = new([]);
        private RuntimeSceneTarget? target;

        public NativeSceneFixture(bool automatic, Action<Scene>? seed = null)
        {
            var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
            this.hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new DispatcherQueueScheduler(dispatcher), IsRunning = true };
            var publisher = new Mock<IOperationResultPublisher>();
            _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Enqueue);
            var results = publisher.Object;
            this.engine = new EngineService(this.hosting, results, nativeCompatibility: this.compatibility);
            this.sync = new SceneEngineSync(this.engine, operationResults: results, hostingContext: this.hosting);
            var project = new Project(new ProjectInfo("Environment fields", Category.Games, this.directory.FullName, "preview.png")) { Name = "Environment fields" };
            var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
            this.Source = Scene.CreateAndHydrate(project, new SceneData
            {
                Id = Guid.NewGuid(),
                Name = "Environment",
                Environment = new SceneEnvironmentData { ExposureMode = mode, PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } },
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
                new OperationStatusReducer());
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

        public void SetMaterialChoices(IReadOnlyList<MaterialPickerResult> choices) => this.materialChoices.OnNext(choices);

        public void MountCookedRoot(string path) => this.engine.MountProjectCookedRoot(path);

        public Task RefreshCookedRootsAsync(params string[] paths) => this.engine.RefreshProjectCookedRootsAsync(paths);

        public Task SuspendCookedContentAsync() => this.engine.SuspendCookedContentAsync();

        public SceneNodeEditorViewModel CreateInspectorHost(IList<SceneNode> selection)
        {
            this.messenger.Register<SceneNodeSelectionRequestMessage>(this, (_, message) => message.Reply(selection));
            return new(this.hosting, new ViewModelToView(Mock.Of<IViewLocator>()), this.messenger, this.Commands, this.documents.Object, default, this.AssetCatalog.Object, this.MaterialPicker.Object, this.sync, new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(), Mock.Of<ISceneContentDemandService>());
        }

        public async Task InitializeAsync(CancellationToken cancellationToken, string? cookedRoot = null)
        {
            _ = (await this.engine.InitializeAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
            this.engine.TargetFps = 60;
            await this.engine.StartAsync().ConfigureAwait(true);
            if (cookedRoot is not null)
            {
                this.MountCookedRoot(cookedRoot);
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
