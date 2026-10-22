// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Numerics;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.Integration.UI.Tests.Authoring;

[TestClass]
internal sealed class BackgroundColorTests : DroidNet.Tests.VisualUserInterfaceTests
{
    // The color the drag ends on.
    private static readonly Vector3 PickedColor = new(100 / 255f, 0.25f, 0.5f);

    public TestContext TestContext { get; set; } = null!;

    /// <summary>A background color drag, its undo/redo and saved reopen converge to the observed native background RGB.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task BackgroundColorDragUndoRedoAndReopenReachTheObservedNativeScene() => EnqueueAsync(async () =>
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
        var project = TestProject.Create(new RealFileSystem(), directory).Context;
        var path = Path.Combine(directory, "Content", "Scenes", "Background.oscene.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var data = new SceneData { Id = SceneId.New(), Name = "Background", Environment = new SceneEnvironmentData { AtmosphereEnabled = true } };
        await File.WriteAllBytesAsync(path, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData), cancellationToken).ConfigureAwait(true);
        var file = new DocumentFile(FileSystemPath.From(path), AssetPath.Parse("/Content/Scenes/Background.oscene"));
        using var session = new TestSession(project);
        using var scenes = new OpenDocuments([new SceneDocumentKind()], new NativeAtomicFileStore(new RealFileSystem()));
        var (previewEngine, probe) = EngineProbe.Of(engine);
        var preview = ScenePreviews.Create(previewEngine, scenes, session, operationResults: results);
        using var previewLifetime = (IDisposable)preview;
        var document = await OpenAndShowAsync(scenes, file, preview, probe, cancellationToken).ConfigureAwait(true);
        await EditSaveBackgroundAsync(engine, document, document.Target, cancellationToken).ConfigureAwait(true);
        scenes.Close(document.Document);
        var reopened = await OpenAndShowAsync(scenes, file, preview, probe, cancellationToken).ConfigureAwait(true);
        var environment = reopened.Scene.Environment;
        _ = environment.Background.ColorRgb.Should().Be(PickedColor);
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

        // Show a solid color that only paints the view: the background.
        _ = (await authoring.ExecuteAsync(new SetSceneProperties(PropertyEdit.SingleEdit(SceneSkyFields.Backdrop, EnvironmentBackdrop.SolidColor), "Backdrop")).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = (await authoring.ExecuteAsync(new SetSceneProperties(PropertyEdit.SingleEdit(SceneSkyFields.SolidColorLightsScene, value: false), "Lights scene")).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var original = await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true);
        _ = original.AtmosphereEnabled.Should().BeFalse();
        await DragBackgroundColorAsync(authoring, cancellationToken).ConfigureAwait(true);
        _ = scene.Environment.Background.ColorRgb.Should().Be(PickedColor);
        _ = opened.Document.History.UndoStack.Should().HaveCount(3, "the drag commits once");
        _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(PickedColor);
        await opened.Document.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(original.Background.ColorRgb);
        await opened.Document.History.RedoAsync(cancellationToken).ConfigureAwait(true);
        _ = (await ReadNativeBackgroundAsync(engine, target, cancellationToken).ConfigureAwait(true)).Background.ColorRgb.Should().Be(PickedColor);
        _ = (await opened.Document.SaveAsync(cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
    }

    // Drags the color through a hundred values, each previewed in the engine, and commits the last as one edit.
    private static async Task DragBackgroundColorAsync(SceneAuthoring authoring, CancellationToken cancellationToken)
    {
        var gesture = await authoring.BeginGestureAsync([], [SceneSkyFields.SolidColor.Id], "Background color").ConfigureAwait(true);
        for (var sample = 1; sample <= 100; sample++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var color = new Vector3(sample / 255f, PickedColor.Y, PickedColor.Z);
            _ = (await gesture.PreviewAsync(new SetSceneProperties(PropertyEdit.SingleEdit(SceneSkyFields.SolidColor, color), "Background color")).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        _ = (await gesture.CommitAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
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
