// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Owns a real Runtime scene and disk source for per-field control qualification.</summary>
public sealed partial class InspectorControlTests
{
    private sealed class NativeEnvironmentFixture : IAsyncDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenEnvironmentField-");
        private readonly ProjectManagerService manager = new(new NativeStorageProvider(new RealFileSystem()));
        private readonly EngineService engine;
        private readonly SceneEngineSync sync;
        private readonly SceneDocumentCommandService commands;
        private RuntimeSceneTarget? target;

        public NativeEnvironmentFixture(bool automatic)
        {
            var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
            var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new DispatcherQueueScheduler(dispatcher), IsRunning = true };
            var results = Mock.Of<IOperationResultPublisher>();
            this.engine = new EngineService(hosting, results);
            this.sync = new SceneEngineSync(this.engine, operationResults: results, hostingContext: hosting);
            var project = new Project(new ProjectInfo("Environment fields", Category.Games, this.directory.FullName, "preview.png")) { Name = "Environment fields" };
            var mode = automatic ? ExposureMode.Auto : ExposureMode.Manual;
            this.Source = Scene.CreateAndHydrate(project, new SceneData
            {
                Id = Guid.NewGuid(),
                Name = "Environment",
                Environment = new SceneEnvironmentData { ExposureMode = mode, PostProcess = new PostProcessEnvironmentData { ExposureMode = mode } },
            });
            project.Scenes.Add(this.Source);
            this.Context = new(this.Source.Id, new SceneDocumentMetadata(this.Source.Id), this.Source, new HistoryKeeper(this.Source));
            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
            this.commands = new SceneDocumentCommandService(
                Mock.Of<ISceneExplorerService>(),
                new SceneSelectionService(),
                this.sync,
                this.manager,
                documents.Object,
                default,
                new StrongReferenceMessenger(),
                results,
                new OperationStatusReducer());
            this.Model = new EnvironmentViewModel(this.commands, () => this.Context);
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

        public async Task InitializeAsync(CancellationToken cancellationToken)
        {
            _ = (await this.engine.InitializeAsync(cancellationToken).ConfigureAwait(true)).Should().BeTrue();
            this.engine.TargetFps = 60;
            await this.engine.StartAsync().ConfigureAwait(true);
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
            _ = (await this.commands.SaveSceneAsync(this.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
            _ = this.Context.Metadata.IsDirty.Should().BeFalse();
            this.Model.SetScene(value: null);
            this.sync.CloseDocument(this.Context.Metadata);
            this.Context.History.Clear();
            var reopened = await this.manager.LoadSceneAsync(this.Source).ConfigureAwait(true);
            _ = reopened.Should().NotBeNull();
            this.Source = reopened!;
            this.Context = new(this.Source.Id, new SceneDocumentMetadata(this.Source.Id), this.Source, new HistoryKeeper(this.Source));
            this.Model.SetScene(this.Source);
            await this.SynchronizeAsync(cancellationToken).ConfigureAwait(true);
        }

        public async ValueTask DisposeAsync()
        {
            this.Model.Dispose();
            await this.Model.PendingEdits.ConfigureAwait(true);
            this.sync.CloseDocument(this.Context.Metadata);
            this.sync.Dispose();
            this.Context.History.Clear();
            try
            {
                await this.engine.DisposeAsync().ConfigureAwait(true);
            }
            finally
            {
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
