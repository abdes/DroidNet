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
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;
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
        var results = Mock.Of<IOperationResultPublisher>();
        var engine = SharedNativeEngine.Engine;
        var engineResults = SharedNativeEngine.RouteResults(new ConcurrentQueue<OperationResult>());
        var directory = Directory.CreateTempSubdirectory("OxygenNativeBackground-");
        try
        {
            _ = (await SharedNativeEngine.EnsureRunningAsync(timeout.Token).ConfigureAwait(true)).Should().BeTrue();
            await CheckNativeBackgroundWorkflowAsync(engine, results, directory.FullName, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            engineResults.Dispose();
            await SharedNativeEngine.ResetAsync().ConfigureAwait(true);
            directory.Delete(recursive: true);
        }
    });

    private static async Task CheckNativeBackgroundWorkflowAsync(EngineService engine, IOperationResultPublisher results, string directory, CancellationToken cancellationToken)
    {
        var project = ProjectContext.FromProjectInfo(new ProjectInfo("Native UI", Category.Games, directory, "preview.png"), new RealFileSystem());
        var path = Path.Combine(directory, "Content", "Scenes", "Background.oscene.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var data = new SceneData { Id = SceneId.New(), Name = "Background", Environment = new SceneEnvironmentData { AtmosphereEnabled = true } };
        await File.WriteAllBytesAsync(path, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData), cancellationToken).ConfigureAwait(true);
        var file = new DocumentFile(FileSystemPath.From(path), AssetPath.Parse("/Content/Scenes/Background.oscene"));
        using var scenes = new OpenDocuments([new SceneDocumentKind(project)], new NativeAtomicFileStore(new RealFileSystem()));
        var (previewEngine, probe) = EngineProbe.Of(engine);
        var preview = ScenePreviews.Create(previewEngine, scenes, operationResults: results);
        using var previewLifetime = (IDisposable)preview;
        var document = await OpenAndShowAsync(scenes, file, preview, probe, cancellationToken).ConfigureAwait(true);
        await EditSaveBackgroundAsync(engine, document, document.Target, cancellationToken).ConfigureAwait(true);
        scenes.Close(document.Document);
        var reopened = await OpenAndShowAsync(scenes, file, preview, probe, cancellationToken).ConfigureAwait(true);
        var environment = reopened.Scene.Environment;
        AssertColorClose(environment.Background.ColorRgb, new Vector3(0.12743768f, 0.05126946f, 0.2158605f));
        _ = environment.Background.Enabled.Should().BeTrue();
        _ = environment.AtmosphereEnabled.Should().BeFalse();
        var observed = await ReadNativeBackgroundAsync(engine, reopened.Target, cancellationToken).ConfigureAwait(true);
        _ = observed.Background.ColorRgb.Should().Be(environment.Background.ColorRgb);
        _ = observed.AtmosphereEnabled.Should().BeFalse();
        scenes.Close(reopened.Document);
    }

    // Opens the scene file as a document; the preview shows it, and the probe names the activation the engine accepted.
    private static async Task<OpenScene> OpenAndShowAsync(OpenDocuments scenes, DocumentFile file, IScenePreview preview, EngineProbe probe, CancellationToken cancellationToken)
    {
        using var changed = new SemaphoreSlim(0);
        void OnReadyChanged(object? sender, ScenePreviewEventArgs args) => changed.Release();
        void OnActivated(object? sender, EventArgs args) => changed.Release();
        preview.ReadyChanged += OnReadyChanged;
        probe.Activated += OnActivated;
        try
        {
            var opened = await scenes.OpenAsync(file, cancellationToken).ConfigureAwait(true);
            _ = opened.Document.Should().NotBeNull(opened.Problem);
            var document = opened.Document!;
            var scene = ((SceneContent)document.Content).Scene;
            while (!(preview.IsReady(document.Id) && probe.LastActivated is { } shown && shown.SceneId == scene.Id))
            {
                await changed.WaitAsync(cancellationToken).ConfigureAwait(true);
            }

            return new(document, scene, probe.LastActivated!.Value);
        }
        finally
        {
            preview.ReadyChanged -= OnReadyChanged;
            probe.Activated -= OnActivated;
        }
    }

    private static async Task EditSaveBackgroundAsync(EngineService engine, OpenScene opened, RuntimeSceneTarget target, CancellationToken cancellationToken)
    {
        var scene = opened.Scene;
        var authoring = ((SceneContent)opened.Document.Content).Authoring;
        var model = new EnvironmentViewModel(() => authoring);
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
            _ = opened.Document.History.UndoStack.Should().HaveCount(3);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(expected);
            await opened.Document.History.UndoAsync(cancellationToken).ConfigureAwait(true);
            _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(original.Background.ColorRgb);
            await opened.Document.History.RedoAsync(cancellationToken).ConfigureAwait(true);
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
