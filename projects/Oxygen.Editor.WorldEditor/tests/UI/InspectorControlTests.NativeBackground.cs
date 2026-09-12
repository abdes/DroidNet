// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Reactive.Concurrency;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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
using Color = Windows.UI.Color;

namespace Oxygen.Editor.World.Tests;

/// <summary>Connects actual inspector controls to the public Runtime service, native state and saved source.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Color-picker authoring, undo/redo and saved reopen converge to observed native background RGB.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task BackgroundPickerUndoRedoAndReopenReachTheObservedNativeScene() => EnqueueAsync(async () =>
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new DispatcherQueueScheduler(dispatcher), IsRunning = true };
        var results = Mock.Of<IOperationResultPublisher>();
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var engine = new EngineService(hosting, results, nativeCompatibility: compatibility);
        await using var engineLifetime = engine.ConfigureAwait(true);
        _ = (await engine.InitializeAsync(timeout.Token).ConfigureAwait(true)).Should().BeTrue();
        engine.TargetFps = 60;
        await engine.StartAsync().ConfigureAwait(true);
        using var sync = new SceneEngineSync(engine, operationResults: results, hostingContext: hosting);
        var directory = Directory.CreateTempSubdirectory("OxygenNativeBackground-");
        try
        {
            await CheckNativeBackgroundWorkflowAsync(engine, sync, results, directory.FullName, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            sync.Dispose();
            await engine.ShutdownAsync().ConfigureAwait(true);
            directory.Delete(recursive: true);
        }
    });

    private static async Task CheckNativeBackgroundWorkflowAsync(EngineService engine, SceneEngineSync sync, IOperationResultPublisher results, string directory, CancellationToken cancellationToken)
    {
        var manager = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var project = new Project(new ProjectInfo("Native UI", Category.Games, directory, "preview.png")) { Name = "Native UI" };
        var scene = Scene.CreateAndHydrate(project, new SceneData { Id = Guid.NewGuid(), Name = "Background", Environment = new SceneEnvironmentData { AtmosphereEnabled = true } });
        project.Scenes.Add(scene);
        var metadata = new SceneDocumentMetadata(scene.Id);
        var ready = new TaskCompletionSource<RuntimeSceneTarget>(TaskCreationOptions.RunContinuationsAsynchronously);
        sync.SceneSynchronized += (_, args) =>
        {
            if (ReferenceEquals(args.Metadata, metadata))
            {
                _ = ready.TrySetResult(args.Target);
            }
        };
        _ = sync.RegisterDocument(scene, metadata).Should().BeTrue();
        _ = (await sync.SyncSceneWhenReadyAsync(scene, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        var target = await ready.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        await EditSaveBackgroundAsync(engine, sync, results, manager, scene, metadata, target, cancellationToken).ConfigureAwait(true);

        sync.CloseDocument(metadata);
        var reopened = await manager.LoadSceneAsync(scene).ConfigureAwait(true);
        _ = reopened.Should().NotBeNull();
        AssertColorClose(reopened!.Environment.BackgroundColor, new Vector3(0.12743768f, 0.05126946f, 0.2158605f));
        _ = reopened.Environment.AtmosphereEnabled.Should().BeFalse();
        metadata = new SceneDocumentMetadata(reopened.Id);
        ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = sync.RegisterDocument(reopened, metadata).Should().BeTrue();
        _ = (await sync.SyncSceneWhenReadyAsync(reopened, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
        target = await ready.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        var observed = await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true);
        _ = observed.Color.Should().Be(reopened.Environment.BackgroundColor);
        _ = observed.AtmosphereEnabled.Should().BeFalse();
        sync.CloseDocument(metadata);
    }

    private static async Task EditSaveBackgroundAsync(
        EngineService engine,
        SceneEngineSync sync,
        IOperationResultPublisher results,
        ProjectManagerService manager,
        Scene scene,
        SceneDocumentMetadata metadata,
        RuntimeSceneTarget target,
        CancellationToken cancellationToken)
    {
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
        var commands = new SceneDocumentCommandService(
            Mock.Of<ISceneExplorerService>(),
            new SceneSelectionService(),
            sync,
            manager,
            documents.Object,
            default,
            new StrongReferenceMessenger(),
            results,
            new OperationStatusReducer());
        var context = new SceneDocumentCommandContext(scene.Id, metadata, scene, new HistoryKeeper(scene));
        var model = new EnvironmentViewModel(commands, () => context);
        using (model)
        {
            model.SetScene(scene);
            var view = new EnvironmentView { ViewModel = model };
            var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
            await LoadTestContentAsync(scroller).ConfigureAwait(true);
            view.FindDescendant<ToggleSwitch>()!.IsOn = false;
            await model.PendingEdits.ConfigureAwait(true);
            var original = await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true);
            _ = original.AtmosphereEnabled.Should().BeFalse();
            await EditBackgroundPickerAsync(view, scroller, cancellationToken).ConfigureAwait(true);
            await model.PendingEdits.ConfigureAwait(true);
            var expected = scene.Environment.BackgroundColor;
            AssertColorClose(expected, new Vector3(0.12743768f, 0.05126946f, 0.2158605f));
            _ = context.History.UndoStack.Should().HaveCount(2);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Color.Should().Be(expected);
            await context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Color.Should().Be(original.Color);
            await context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Color.Should().Be(expected);
            _ = (await commands.SaveSceneAsync(context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }
    }

    private static async Task EditBackgroundPickerAsync(EnvironmentView view, ScrollViewer scroller, CancellationToken cancellationToken)
    {
        var vector = await FindVisibleVectorAsync(view, scroller, "BackgroundColor").ConfigureAwait(true);
        var button = ((Grid)vector.Parent).Children.OfType<Button>().Single();
        var flyout = (Flyout)button.Flyout;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        flyout.Closed += (_, _) => closed.TrySetResult();
        flyout.ShowAt(button);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var picker = (ColorPicker)flyout.Content;
        for (var sample = 1; sample <= 100; sample++)
        {
            picker.Color = Color.FromArgb(255, (byte)sample, 64, 128);
        }

        flyout.Hide();
        await closed.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
    }

    private static async Task<RuntimeBackgroundState> ReadNativeBackgroundAsync(EngineService engine, RuntimeSceneTarget target, CancellationToken cancellationToken)
    {
        var observed = await engine.WorldCommands.ObserveBackgroundAsync(Guid.NewGuid(), target, cancellationToken).ConfigureAwait(true);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.Exists.Should().BeTrue();
        return observed.State;
    }
}
