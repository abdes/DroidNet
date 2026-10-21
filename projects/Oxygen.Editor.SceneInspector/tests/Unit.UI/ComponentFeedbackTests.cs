// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using static Oxygen.Editor.SceneInspector.TestSupport.InspectorControls;
using static Oxygen.Editor.SceneInspector.TestSupport.SceneTestData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

[TestClass]
internal sealed class ComponentFeedbackTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Filtering retains rejected values' feedback without adding history or hiding the error indicator.</summary>
    /// <param name="kind">The component whose field is rejected.</param>
    /// <returns>The asynchronous feedback regression.</returns>
    [TestMethod]
    [DataRow("Camera")]
    [DataRow("Light")]
    [DataRow("Transform")]
    public Task ComponentErrorsRemainDiscoverableAcrossFilters(string kind) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost(kind, realizeViews: true);
        var type = FeedbackType(kind);
        var editor = (ComponentPropertyEditor)host.PropertyEditors.Single(item => MatchesInspector(item, kind));
        RejectComponentValue(editor);
        await this.WaitForFeedbackAsync(editor).ConfigureAwait(true);
        var message = editor.ValidationFeedback!.Summary;
        var view = new SceneNodeEditorView
        {
            ViewModel = host,
            Width = 360,
            Height = 650,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        host.SelectComponentFilter(type == typeof(TransformComponent) ? typeof(PerspectiveCamera) : typeof(TransformComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        var option = host.ComponentFilters.Single(item => item.ComponentType == type);
        _ = option.ValidationMessage.Should().Be(message);
        _ = option.ToolTip.Should().Contain(message);
        _ = ComponentButton(view, type).FindDescendant<FontIcon>(icon => string.Equals(icon.Glyph, "\uEA39", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Visible);
        host.SelectComponentFilter(type);
        _ = editor.ValidationFeedback.Summary.Should().Be(message);
        _ = fixture.Document.IsDirty.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        AcceptComponentValue(editor);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = editor.ValidationFeedback.Summary.Should().BeEmpty();
        _ = option.HasValidationErrors.Should().BeFalse();
    });

    /// <summary>A field error stays with its component while the component's section is hidden.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task FieldErrorSurvivesHidingItsComponent() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane.Value = float.NaN;
        await camera.PendingEdits.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);

        host.SelectComponentFilter(typeof(TransformComponent));

        var code = camera.NearPlaneDiagnostic.Code;
        _ = code.Should().NotBeEmpty();
        _ = host.ComponentFilters.Single(option => option.ComponentType == typeof(PerspectiveCamera)).HasValidationErrors.Should().BeTrue();
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        _ = camera.NearPlaneDiagnostic.Code.Should().Be(code);
    });

    /// <summary>Errors do not carry from a hidden component into a new node selection.</summary>
    /// <returns>The asynchronous selection-scope regression.</returns>
    [TestMethod]
    public Task HiddenComponentErrorsResetForNewTargets() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane.Value = 2000;
        await camera.PendingEdits.ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        host.SelectComponentFilter(typeof(TransformComponent));
        var other = AddNumericNode(fixture.Scene, 80);
        fixture.Select(other);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = camera.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = host.ComponentFilters.Should().OnlyContain(option => !option.HasValidationErrors);
    });

    /// <summary>Source corrections update the feedback of a hidden section.</summary>
    /// <returns>The asynchronous source-refresh regression.</returns>
    [TestMethod]
    public Task CorrectingHiddenComponentSourceClearsItsError() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        camera.NearPlane.Value = 2000;
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
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Camera", realizeViews: true);
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        var camera = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        var view = new SceneNodeEditorView
        {
            ViewModel = host,
            Width = 360,
            Height = 650,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
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
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        RaiseNumberEvent(current, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await camera.PendingEdits.ConfigureAwait(true);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    });

    private static void RejectComponentValue(ComponentPropertyEditor editor)
    {
        switch (editor)
        {
            case PerspectiveCameraViewModel camera:
                camera.NearPlane.Value = 2000;
                break;
            case DirectionalLightViewModel light:
                light.ColorR = float.NaN;
                break;
            case TransformViewModel transform:
                transform.ScaleX = 0;
                break;
        }
    }

    private static void AcceptComponentValue(ComponentPropertyEditor editor)
    {
        switch (editor)
        {
            case PerspectiveCameraViewModel camera:
                camera.NearPlane.Value = 0.2f;
                break;
            case DirectionalLightViewModel light:
                light.ColorR = 0.75f;
                break;
            case TransformViewModel transform:
                transform.ScaleX = 2;
                break;
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
