// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Verifies component-scoped feedback while property sections are hidden.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Filtering retains rejected values' feedback without adding history or hiding the error indicator.</summary>
    /// <param name="kind">The component whose field is rejected.</param>
    /// <returns>The asynchronous feedback regression.</returns>
    [TestMethod]
    [DataRow("Camera")]
    [DataRow("Light")]
    [DataRow("Transform")]
    public Task ComponentErrorsRemainDiscoverableAcrossFilters(string kind) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind, realizeViews: true);
        var type = FeedbackType(kind);
        var editor = (ComponentPropertyEditor)host.PropertyEditors.Single(item => MatchesInspector(item, kind));
        RejectComponentValue(editor);
        await this.WaitForFeedbackAsync(editor).ConfigureAwait(true);
        var message = editor.ValidationFeedback!.Summary;
        var view = new SceneNodeEditorView { ViewModel = host, Width = 360, Height = 650 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        host.SelectComponentFilter(type == typeof(TransformComponent) ? typeof(PerspectiveCamera) : typeof(TransformComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        var option = host.ComponentFilters.Single(item => item.ComponentType == type);
        _ = option.ValidationMessage.Should().Be(message);
        _ = option.ToolTip.Should().Contain(message);
        _ = ComponentButton(view, type).FindDescendant<FontIcon>(icon => string.Equals(icon.Glyph, "\uEA39", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Visible);
        host.SelectComponentFilter(type);
        _ = editor.ValidationFeedback.Summary.Should().Be(message);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        AcceptComponentValue(editor);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = editor.ValidationFeedback.Summary.Should().BeEmpty();
        _ = option.HasValidationErrors.Should().BeFalse();
    });

    /// <summary>A result for the current targets remains valid when its section becomes hidden before completion.</summary>
    /// <returns>The asynchronous pending-result regression.</returns>
    [TestMethod]
    public Task PendingFieldErrorCanCompleteWhileComponentIsHidden() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var pending = new TaskCompletionSource<SceneCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var commands = new Mock<ISceneDocumentCommandService>();
        _ = commands.Setup(service => service.EditPropertiesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<PropertyEdit>(), It.IsAny<string>(), It.IsAny<EditSessionToken>())).Returns(pending.Task);
        using var host = fixture.CreateInspectorHost("Camera", commandService: commands.Object);
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane = 2;
        host.SelectComponentFilter(typeof(TransformComponent));
        pending.SetResult(new(Succeeded: false) { ValidationCode = "TEST_REJECTED", ValidationMessage = "Pending field rejected." });
        await camera.PendingEdits.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().Be("Pending field rejected.");
        _ = host.ComponentFilters.Single(option => option.ComponentType == typeof(PerspectiveCamera)).HasValidationErrors.Should().BeTrue();
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        _ = camera.NearPlaneDiagnostic.Message.Should().Be("Pending field rejected.");
    });

    /// <summary>Errors do not carry from a hidden component into a new node selection.</summary>
    /// <returns>The asynchronous selection-scope regression.</returns>
    [TestMethod]
    public Task HiddenComponentErrorsResetForNewTargets() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane = 2000;
        await camera.PendingEdits.ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        host.SelectComponentFilter(typeof(TransformComponent));
        var other = AddNumericNode(fixture.Scene, 80);
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([other]));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = host.ComponentFilters.Should().OnlyContain(option => !option.HasValidationErrors);
    });

    /// <summary>Source corrections update the feedback of a hidden section.</summary>
    /// <returns>The asynchronous source-refresh regression.</returns>
    [TestMethod]
    public Task CorrectingHiddenComponentSourceClearsItsError() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane = 2000;
        await camera.PendingEdits.ConfigureAwait(true);
        host.SelectComponentFilter(typeof(TransformComponent));
        _ = camera.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        fixture.Camera.NearPlane = 0.2f;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = host.ComponentFilters.Single(option => option.ComponentType == typeof(PerspectiveCamera)).HasValidationErrors.Should().BeFalse();
    });

    /// <summary>Old controls cannot edit or finish a new gesture after their section is recreated.</summary>
    /// <returns>The asynchronous control-lifetime regression.</returns>
    [TestMethod]
    public Task RemovedSectionControlsCannotChangeReactivatedEditor() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Camera", realizeViews: true);
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        var view = new SceneNodeEditorView { ViewModel = host, Width = 360, Height = 650 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var oldNumber = view.FindDescendant<NumberBox>(control => Equals(control.Tag, "FieldOfView"))!;
        host.SelectComponentFilter(typeof(TransformComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        await WaitForRenderAsync().ConfigureAwait(true);
        var current = view.FindDescendant<NumberBox>(control => Equals(control.Tag, "FieldOfView"))!;
        _ = current.Should().NotBeSameAs(oldNumber);
        RaiseNumberEvent(current, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        current.NumberValue = 70;
        oldNumber.NumberValue = 100;
        RaiseNumberEvent(oldNumber, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await camera.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.FieldOfView.Should().Be(70);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        RaiseNumberEvent(current, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await camera.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    private static Type FeedbackType(string kind) => kind switch
    {
        "Camera" => typeof(PerspectiveCamera),
        "Light" => typeof(DirectionalLightComponent),
        _ => typeof(TransformComponent),
    };

    private static void RejectComponentValue(ComponentPropertyEditor editor)
    {
        switch (editor)
        {
            case PerspectiveCameraViewModel camera: camera.NearPlane = 2000; break;
            case DirectionalLightViewModel light: light.ColorR = float.NaN; break;
            case TransformViewModel transform: transform.ScaleX = 0; break;
        }
    }

    private static void AcceptComponentValue(ComponentPropertyEditor editor)
    {
        switch (editor)
        {
            case PerspectiveCameraViewModel camera: camera.NearPlane = 0.2f; break;
            case DirectionalLightViewModel light: light.ColorR = 0.75f; break;
            case TransformViewModel transform: transform.ScaleX = 2; break;
        }
    }

    private async Task WaitForFeedbackAsync(ComponentPropertyEditor editor)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        while (editor.ValidationFeedback?.Summary.Length == 0)
        {
            await Task.Delay(10, timeout.Token).ConfigureAwait(true);
        }
    }
}
