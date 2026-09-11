// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Assets.Catalog;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises numeric control sessions with the inspector's real selection and model observers.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A hundred bound samples produce one history entry and throttled previews followed by a terminal sync.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlGroupsSamplesAndHistoryRefreshesTheInspector(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        _ = number.Should().NotBeNull();
        var before = number.NumberValue;
        using var throttle = new SceneEngineSync(Mock.Of<IEngineService>());
        var previews = new List<DateTimeOffset>();
        var terminals = 0;
        fixture.ConfigureObservedSync(throttle, previews, () => terminals++);

        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = before + (sample / 100f);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = terminals.Should().Be(1);
        _ = previews.Should().NotBeEmpty();
        _ = previews.Zip(previews.Skip(1), (first, second) => second - first).Should().OnlyContain(interval => interval >= TimeSpan.FromMilliseconds(16));
        _ = number.NumberValue.Should().Be(before + 1);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before + 1);
    });

    /// <summary>Cancellation restores a numeric preview without making a history entry.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlCancellationRestoresTheBoundModel(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        var before = number.NumberValue;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = before + 1;
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before + 1);
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Cancel);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Wheel samples share a session until the idle delay has elapsed.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    [DataRow("Environment", "PlanetRadiusKm")]
    public Task NumericControlWheelCommitsOnlyAfterIdle(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        var before = number.NumberValue;
        for (var tick = 1; tick <= 4; tick++)
        {
            RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.MouseWheel);
            number.NumberValue = before + tick;
            RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.MouseWheel, NumberBoxEditCompletionKind.Commit);
        }

        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(before + 4);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });

    private static bool MatchesInspector(IPropertyEditor<SceneNode> model, string kind) => kind switch
    {
        "Camera" => model is PerspectiveCameraViewModel,
        "Light" => model is DirectionalLightViewModel,
        "Environment" => model is EnvironmentViewModel,
        _ => false,
    };

    private static UserControl CreateNumericView(IPropertyEditor<SceneNode> model) => model switch
    {
        PerspectiveCameraViewModel camera => new PerspectiveCameraView { ViewModel = camera },
        DirectionalLightViewModel light => new DirectionalLightView { ViewModel = light },
        EnvironmentViewModel environment => new EnvironmentView { ViewModel = environment },
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    private static Task PendingNumericEdits(IPropertyEditor<SceneNode> model) => model switch
    {
        PerspectiveCameraViewModel camera => camera.PendingEdits,
        DirectionalLightViewModel light => light.PendingEdits,
        EnvironmentViewModel environment => environment.PendingEdits,
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    private sealed partial class Fixture
    {
        public SceneNodeEditorViewModel CreateInspectorHost(string kind)
        {
            IList<SceneNode> selection = string.Equals(kind, "Environment", StringComparison.Ordinal) ? [] : [this.Node];
            this.Messenger.Register<SceneNodeSelectionRequestMessage>(this, (_, message) => message.Reply(selection));
            _ = this.Documents.Setup(service => service.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Context.Metadata]);
            _ = this.Documents.Setup(service => service.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(this.Scene.Id);
            _ = this.Sync.Setup(service => service.GetDocumentScene(this.Context.Metadata)).Returns(this.Scene);
            var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
            return new(
                new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) },
                new ViewModelToView(Mock.Of<IViewLocator>()),
                this.Messenger,
                this.Commands,
                this.Documents.Object,
                default,
                Mock.Of<IAssetCatalog>(),
                Mock.Of<IMaterialPickerService>(),
                this.Sync.Object);
        }

        public void ConfigureObservedSync(SceneEngineSync throttle, List<DateTimeOffset> previews, Action terminal)
        {
            _ = this.Sync.Setup(value => value.TryPreviewSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<DateTimeOffset>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
                .Returns((Guid sceneId, Guid nodeId, DateTimeOffset now, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) =>
                    throttle.TryPreviewSyncAsync(
                        sceneId,
                        nodeId,
                        now,
                        ct =>
                    {
                        previews.Add(now);
                        return action(ct);
                    },
                        token));
            _ = this.Sync.Setup(value => value.CompleteTerminalSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
                .Returns((Guid sceneId, Guid nodeId, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) =>
                    throttle.CompleteTerminalSyncAsync(
                        sceneId,
                        nodeId,
                        ct =>
                    {
                        terminal();
                        return action(ct);
                    },
                        token));
        }
    }
}
