// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Numerics;
using System.Reactive.Concurrency;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using CommunityToolkit.WinUI.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage.Native;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldControls;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;
using Color = Windows.UI.Color;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.SceneEditor;

[TestClass]
public sealed partial class BackgroundColorTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Color-picker authoring, undo/redo and saved reopen converge to observed native background RGB.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task BackgroundPickerUndoRedoAndReopenReachTheObservedNativeScene() => EnqueueAsync(async () =>
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        var hosting = new HostingContext
        {
            Application = Application.Current,
            Dispatcher = dispatcher,
            DispatcherScheduler = new DispatcherQueueScheduler(dispatcher),
            IsRunning = true,
        };
        var results = Mock.Of<IOperationResultPublisher>();
        var engine = SharedNativeEngine.Engine;
        var engineResults = SharedNativeEngine.RouteResults(new ConcurrentQueue<OperationResult>());
        var directory = Directory.CreateTempSubdirectory("OxygenNativeBackground-");
        try
        {
            _ = (await SharedNativeEngine.EnsureRunningAsync(timeout.Token).ConfigureAwait(true)).Should().BeTrue();
            using var sync = new SceneEngineSync(engine, operationResults: results, hostingContext: hosting);
            await CheckNativeBackgroundWorkflowAsync(engine, sync, results, directory.FullName, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            engineResults.Dispose();
            await SharedNativeEngine.ResetAsync().ConfigureAwait(true);
            directory.Delete(recursive: true);
        }
    });

    private static async Task CheckNativeBackgroundWorkflowAsync(EngineService engine, SceneEngineSync sync, IOperationResultPublisher results, string directory, CancellationToken cancellationToken)
    {
        var project = new Project(new ProjectInfo("Native UI", Category.Games, directory, "preview.png"))
        {
            Name = "Native UI",
        };
        var path = Path.Combine(directory, "Content", "Scenes", "Background.oscene.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var data = new SceneData { Id = Guid.NewGuid(), Name = "Background", Environment = new SceneEnvironmentData { AtmosphereEnabled = true } };
        await File.WriteAllBytesAsync(path, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData), cancellationToken).ConfigureAwait(true);
        var file = new DocumentFile(path, new Uri("asset:///Content/Scenes/Background.oscene.json"));
        var commands = new SceneDocumentCommandService(new SceneSelectionService(), sync, new StrongReferenceMessenger(), results, new OperationStatusReducer(), Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(), Moq.Mock.Of<Oxygen.Editor.Projects.IProjectContextService>(), Moq.Mock.Of<ISceneMutator>(), Moq.Mock.Of<ISceneOrganizer>());
        var projects = new Mock<IProjectManagerService>();
        _ = projects.SetupGet(value => value.CurrentProject).Returns(project);
        using var scenes = new OpenDocuments([new SceneDocumentKind(projects.Object, commands)], new NativeAtomicFileStore(new RealFileSystem()));
        var document = await OpenAndShowAsync(scenes, file, sync, cancellationToken).ConfigureAwait(true);
        await EditSaveBackgroundAsync(engine, commands, document, document.Target, cancellationToken).ConfigureAwait(true);
        sync.CloseDocument(document.Document.Id);
        scenes.Close(document.Document);
        var reopened = await OpenAndShowAsync(scenes, file, sync, cancellationToken).ConfigureAwait(true);
        var environment = reopened.Scene.Environment;
        AssertColorClose(environment.Background.ColorRgb, new Vector3(0.12743768f, 0.05126946f, 0.2158605f));
        _ = environment.Background.Enabled.Should().BeTrue();
        _ = environment.AtmosphereEnabled.Should().BeFalse();
        var observed = await ReadNativeBackgroundAsync(engine, reopened.Target, cancellationToken).ConfigureAwait(true);
        _ = observed.Background.ColorRgb.Should().Be(environment.Background.ColorRgb);
        _ = observed.AtmosphereEnabled.Should().BeFalse();
        sync.CloseDocument(reopened.Document.Id);
    }

    // Opens the scene file as a document and shows its scene in the engine.
    private static async Task<OpenScene> OpenAndShowAsync(OpenDocuments scenes, DocumentFile file, SceneEngineSync sync, CancellationToken cancellationToken)
    {
        var opened = await scenes.OpenAsync(file, cancellationToken).ConfigureAwait(true);
        _ = opened.Document.Should().NotBeNull(opened.Problem);
        var document = opened.Document!;
        var scene = ((SceneContent)document.Content).Scene;
        var ready = new TaskCompletionSource<RuntimeSceneTarget>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnSynchronized(object? sender, SceneSynchronizationCompletedEventArgs args)
        {
            if (args.DocumentId == document.Id)
            {
                _ = ready.TrySetResult(sync.GetSynchronizedTarget(args.DocumentId)!);
            }
        }

        sync.SceneSynchronized += OnSynchronized;
        try
        {
            _ = sync.RegisterDocument(scene, document.Id).Should().BeTrue();
            _ = (await sync.SyncSceneWhenReadyAsync(scene, cancellationToken).ConfigureAwait(true)).Should().BeTrue();
            return new(document, scene, await ready.Task.WaitAsync(cancellationToken).ConfigureAwait(true));
        }
        finally
        {
            sync.SceneSynchronized -= OnSynchronized;
        }
    }

    private static async Task EditSaveBackgroundAsync(EngineService engine, SceneDocumentCommandService commands, OpenScene opened, RuntimeSceneTarget target, CancellationToken cancellationToken)
    {
        var scene = opened.Scene;
        var context = new SceneDocumentCommandContext(opened.Document.Id, scene, opened.Document.History);
        var model = new EnvironmentViewModel(commands, () => context);
        using (model)
        {
            model.SetScene(scene);
            var view = new EnvironmentView
            {
                ViewModel = model,
            };
            var scroller = new ScrollViewer
            {
                Content = view,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            };
            await LoadTestContentAsync(scroller).ConfigureAwait(true);
            // Show a solid color that only paints the view: the background.
            ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "backdrop";
            var backdrop = (Segmented)await FindInspectorControlAsync(scroller, () => view.FindDescendant<Segmented>(element => Equals(element.Tag, "Backdrop")), "Backdrop", cancellationToken).ConfigureAwait(true);
            backdrop.SelectedIndex = 2;
            await model.PendingEdits.ConfigureAwait(true);
            var lightsScene = (ToggleSwitch)await FindInspectorControlAsync(scroller, () => view.FindDescendant<ToggleSwitch>(element => Equals(element.Tag, "SolidColorLightsScene")), "SolidColorLightsScene", cancellationToken).ConfigureAwait(true);
            lightsScene.IsOn = false;
            await model.PendingEdits.ConfigureAwait(true);
            var original = await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true);
            _ = original.AtmosphereEnabled.Should().BeFalse();
            ((TextBox)view.FindName("ScenePropertySearchBox")).Text = string.Empty;
            await EditBackgroundPickerAsync(view, scroller, cancellationToken).ConfigureAwait(true);
            await model.PendingEdits.ConfigureAwait(true);
            var expected = scene.Environment.Background.ColorRgb;
            AssertColorClose(expected, new Vector3(0.12743768f, 0.05126946f, 0.2158605f));
            _ = context.History.UndoStack.Should().HaveCount(3);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(expected);
            await context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(original.Background.ColorRgb);
            await context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(expected);
            _ = (await opened.Document.SaveAsync(cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }
    }

    private static async Task EditBackgroundPickerAsync(EnvironmentView view, ScrollViewer scroller, CancellationToken cancellationToken)
    {
        _ = await FindVisibleVectorAsync(view, scroller, "SolidColor").ConfigureAwait(true);

        // The restructured inspector hosts the swatch as PropertyCard leading content, so the picker button is an
        // owned accessory of the background field rather than a sibling of the vector box.
        var button = view.FindDescendant<Button>(element => string.Equals(AutomationProperties.GetName(element), "Pick backdrop color", StringComparison.Ordinal));
        _ = button.Should().NotBeNull("the swatch realizes as leading content of the background field");
        var flyout = (Flyout)button!.Flyout;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        flyout.Closed += (_, _) => closed.TrySetResult();
        flyout.ShowAt(button);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        var picker = (ColorPicker)flyout.Content;
        for (var sample = 1; sample <= 100; sample++)
        {
            picker.Color = Color.FromArgb(255, (byte)sample, 64, 128);
        }

        flyout.Hide();
        await closed.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
    }

    private static async Task<RuntimeEnvironmentState> ReadNativeBackgroundAsync(EngineService engine, RuntimeSceneTarget target, CancellationToken cancellationToken)
    {
        var observed = await engine.WorldCommands.ObserveEnvironmentAsync(Guid.NewGuid(), target, cancellationToken).ConfigureAwait(true);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State.Should().NotBeNull();
        _ = observed.State!.BackgroundExists.Should().BeTrue();
        return observed.State;
    }

    private sealed record OpenScene(Document Document, Scene Scene, RuntimeSceneTarget Target);
}
