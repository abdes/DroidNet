// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks unfinished edits and source corrections across component filters.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A filter change keeps accepted text and cancels an unfinished drag.</summary>
    /// <param name="kind">The component being edited.</param>
    /// <param name="text">Whether to edit text rather than preview a drag.</param>
    /// <returns>The asynchronous edit-boundary regression.</returns>
    [TestMethod]
    [DataRow("Camera", true)]
    [DataRow("Camera", false)]
    [DataRow("Light", true)]
    [DataRow("Light", false)]
    [DataRow("Transform", true)]
    [DataRow("Transform", false)]
    public Task ComponentFilterEndsNumericEditAccordingToItsInteraction(string kind, bool text) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost(kind, realizeViews: true);
        var type = FeedbackType(kind);
        host.SelectComponentFilter(type);
        var editor = host.PropertyEditors.Single();
        var view = new SceneNodeEditorView { ViewModel = host, Width = 450, Height = 650 };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        var scroller = (ScrollViewer)view.FindName("PropertyScroll");
        var number = (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => editor is TransformViewModel
                ? view.FindDescendant<VectorBox>(vector => vector.IsLoaded)?.FindDescendant<NumberBox>(control => string.Equals(control.Name, "PartNumberBoxX", StringComparison.Ordinal))
                : view.FindDescendant<NumberBox>(control => control.IsLoaded && Equals(control.Tag, editor is PerspectiveCameraViewModel ? "FieldOfView" : "IntensityLux")),
            kind,
            this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = number.IsLoaded.Should().BeTrue();
        var completions = new List<NumberBoxEditSessionEventArgs>();
        number.EditSessionCompleted += (_, args) => completions.Add(args);
        var before = number.NumberValue;
        var edited = before + 1;
        if (text)
        {
            await EnterTextAsync(number, edited.ToString(CultureInfo.InvariantCulture)).ConfigureAwait(true);
        }
        else
        {
            RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
            number.NumberValue = edited;
        }

        _ = completions.Should().BeEmpty("the field must still be editing when the filter changes");
        host.SelectComponentFilter(type == typeof(TransformComponent) ? typeof(PerspectiveCamera) : typeof(TransformComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        if (editor is not TransformViewModel)
        {
            await PendingNumericEdits(editor).ConfigureAwait(true);
        }

        float ReadValue() => editor is TransformViewModel ? fixture.Node.Components.OfType<TransformComponent>().Single().LocalPosition.X : ReadNumericSource(fixture.Node, kind);
        _ = ReadValue().Should().Be(text ? edited : before, "the filter boundary must finish the original interaction before detaching its controls");
        _ = fixture.Context.History.UndoStack.Should().HaveCount(text ? 1 : 0);
        if (text)
        {
            await fixture.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = ReadValue().Should().Be(before, "Undo must restore the value before the text edit");
            await fixture.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = ReadValue().Should().Be(edited, "Redo must restore the accepted text edit");
        }
    });

    /// <summary>Leaving invalid transform text retains its explanation without editing the source.</summary>
    /// <returns>The asynchronous invalid-input boundary regression.</returns>
    [TestMethod]
    public Task ComponentFilterRetainsRejectedTransformTextFeedback() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Transform", realizeViews: true);
        host.SelectComponentFilter(typeof(TransformComponent));
        var transform = host.PropertyEditors.OfType<TransformViewModel>().Single();
        var view = new SceneNodeEditorView { ViewModel = host, Width = 450, Height = 650 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var scroller = (ScrollViewer)view.FindName("PropertyScroll");
        var number = (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(card => string.Equals(card.PropertyName, "Scale", StringComparison.Ordinal))?
                .FindDescendant<NumberBox>(control => string.Equals(control.Name, "PartNumberBoxX", StringComparison.Ordinal)),
            "ScaleX",
            this.TestContext.CancellationToken).ConfigureAwait(true);
        await EnterTextAsync(number, "0").ConfigureAwait(true);
        var message = transform.ScaleXDiagnostic.Message;
        _ = message.Should().NotBeEmpty();
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = transform.ScaleXDiagnostic.Message.Should().Be(message);
        _ = host.ComponentFilters.Single(option => option.ComponentType == typeof(TransformComponent)).ValidationMessage.Should().Be(message);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Changing a hidden transform clears only the feedback for its changed field.</summary>
    /// <param name="mixed">Whether the changed target is a non-leading member of a mixed selection.</param>
    /// <returns>The asynchronous source-correction regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task HiddenTransformSourceCorrectionClearsObsoleteFieldFeedback(bool mixed) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Transform");
        var target = fixture.Node.Components.OfType<TransformComponent>().Single();
        if (mixed)
        {
            var second = AddNumericNode(fixture.Scene, 80);
            target = second.Components.OfType<TransformComponent>().Single();
            target.LocalScale = new(2, 1, 1);
            _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        }

        var transform = host.PropertyEditors.OfType<TransformViewModel>().Single();
        transform.ScaleX = 0;
        transform.ScaleY = 0;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = transform.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        var otherError = transform.ScaleYDiagnostic.Message;
        _ = otherError.Should().NotBeEmpty();
        host.SelectComponentFilter(typeof(PerspectiveCamera));
        target.LocalScale = new(3, 1, 1);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = transform.ScaleXDiagnostic.Message.Should().BeEmpty();
        _ = transform.ScaleYDiagnostic.Message.Should().Be(otherError);
        _ = host.ComponentFilters.Single(option => option.ComponentType == typeof(TransformComponent)).ValidationMessage.Should().Be(otherError);
    });

    /// <summary>A picker from an old filter lifetime cannot finish the new picker's gesture.</summary>
    /// <returns>The asynchronous color-lifetime regression.</returns>
    [TestMethod]
    public Task OldColorPickerUnloadingCannotFinishReactivatedComponentEdit() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Light");
        host.SelectComponentFilter(typeof(DirectionalLightComponent));
        var light = host.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        var original = ReadSceneColor(fixture, "Light");
        var oldPicker = new ColorPicker();
        var currentPicker = new ColorPicker();
        var panel = new StackPanel();
        panel.Children.Add(oldPicker);
        panel.Children.Add(currentPicker);
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        InspectorColorGestures.Attach(oldPicker, light, "Color");
        InspectorColorGestures.Apply(oldPicker, owner => ((DirectionalLightViewModel)owner).SetColor(Colors.Red));
        host.SelectComponentFilter(typeof(TransformComponent));
        await light.PendingEdits.ConfigureAwait(true);
        _ = ReadSceneColor(fixture, "Light").Should().Be(original);
        host.SelectComponentFilter(typeof(DirectionalLightComponent));
        InspectorColorGestures.Attach(currentPicker, light, "Color");
        InspectorColorGestures.Apply(currentPicker, owner => ((DirectionalLightViewModel)owner).SetColor(Colors.Lime));
        _ = panel.Children.Remove(oldPicker);
        await WaitForRenderAsync().ConfigureAwait(true);
        await light.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty("unloading the old picker must not commit the current gesture");
        _ = ReadSceneColor(fixture, "Light").Should().Be(new Vector3(0, 1, 0));
        light.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await light.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        await fixture.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = ReadSceneColor(fixture, "Light").Should().Be(original);
    });
}
