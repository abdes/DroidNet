// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks dependent field feedback through actual numeric controls.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A valid far-plane edit clears its dependent near-plane error while retaining an unrelated error.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CameraControlDependentCorrectionKeepsUnrelatedFeedbackScoped() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var host = fixture.CreateInspectorHost("Camera");
        var model = host.PropertyEditors.OfType<PerspectiveCameraViewModel>().Single();
        var view = new PerspectiveCameraView { ViewModel = model };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var near = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "NearPlane"))!;
        var far = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "FarPlane"))!;
        var aspect = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "AspectRatio"))!;
        await EnterTextAsync(near, "2000").ConfigureAwait(true);
        near.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        await EnterTextAsync(aspect, "0").ConfigureAwait(true);
        aspect.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = model.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = model.FarPlaneDiagnostic.Message.Should().NotBeEmpty();
        var unrelatedError = model.AspectRatioDiagnostic.Message;
        _ = unrelatedError.Should().NotBeEmpty();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();

        await EnterTextAsync(far, "3000").ConfigureAwait(true);
        far.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = model.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = model.FarPlaneDiagnostic.Message.Should().BeEmpty();
        _ = model.AspectRatioDiagnostic.Message.Should().Be(unrelatedError);
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(text => text.Text == unrelatedError && text.Visibility == Microsoft.UI.Xaml.Visibility.Visible);
        var second = AddNumericNode(fixture.Scene, 80);
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([second]));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        _ = model.AspectRatioDiagnostic.Message.Should().BeEmpty();
        _ = aspect.NumberValue.Should().Be(second.Components.OfType<PerspectiveCamera>().Single().AspectRatio);
    });
}
