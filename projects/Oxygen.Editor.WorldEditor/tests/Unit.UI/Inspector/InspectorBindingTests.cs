// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class InspectorBindingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Each inspector realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <param name="kind">The inspector to realize.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Transform")]
    [DataRow("Camera")]
    [DataRow("Light")]
    [DataRow("Environment")]
    public Task InspectorXamlLoadsItsNumericControls(string kind) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = CreateModel(kind, fixture);
        var view = kind switch
        {
            "Transform" => (UserControl)new TransformView
            {
                ViewModel = (TransformViewModel)model
            },
            "Camera" => new PerspectiveCameraView
            {
                ViewModel = (PerspectiveCameraViewModel)model
            },
            "Light" => new DirectionalLightView
            {
                ViewModel = (DirectionalLightViewModel)model
            },
            "Environment" => new EnvironmentView
            {
                ViewModel = (EnvironmentViewModel)model
            },
            "Material" => new MaterialEditorView
            {
                ViewModel = (MaterialEditorViewModel)model
            },
            _ => throw new ArgumentOutOfRangeException(nameof(kind)),
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        if (view is MaterialEditorView)
        {
            var scroller = view.FindDescendant<ScrollViewer>()!;
            _ = await FindInspectorControlAsync(scroller, () => view.FindDescendant<NumberBox>(number => Equals(number.Tag, "RoughnessFactor")), "Material.RoughnessFactor", this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = view.KeyboardAccelerators.Should().OnlyContain(accelerator => ReferenceEquals(accelerator.ScopeOwner, view));
        }
        else
        {
            _ = view.FindDescendant<NumberBox>().Should().NotBeNull();
        }
    });

    /// <summary>The material editor uses its real XAML and a loaded material document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task MaterialEditorXamlLoadsItsNumericControls() => this.InspectorXamlLoadsItsNumericControls("Material");

    /// <summary>The realized near-plane editor rejects an invalid commit and displays current inline feedback.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task CameraTextCommitRejectsInvalidNearPlaneAndClearsItsDiagnosticOnCorrection() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (PerspectiveCameraViewModel)CreateModel("Camera", fixture);
        var view = new PerspectiveCameraView
        {
            ViewModel = model
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, "NearPlane"))!;
        var before = fixture.Camera.NearPlane;
        await EnterTextAsync(number, "2000").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(before);
        _ = number.NumberValue.Should().Be(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.NearPlaneDiagnostic.Message.Should().NotBeEmpty();
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(element => string.Equals(element.Text, model.NearPlaneDiagnostic.Message, StringComparison.Ordinal) && element.Visibility == Visibility.Visible);
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.NearPlane.Should().Be(2);
        _ = model.NearPlaneDiagnostic.Message.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>A zero scale axis shows scoped inline feedback without changing the model or history.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task TransformTextValidationPreservesScaleUntilAValidCommit() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (TransformViewModel)CreateModel("Transform", fixture);
        var view = new TransformView
        {
            ViewModel = model
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var card = view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Scale", StringComparison.Ordinal))!;
        card.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        var number = card.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        await EnterTextAsync(number, "0").ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(1);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = model.ScaleXDiagnostic.Message.Should().NotBeEmpty();
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(element => string.Equals(element.Text, model.ScaleXDiagnostic.Message, StringComparison.Ordinal) && element.Visibility == Visibility.Visible);
        await EnterTextAsync(number, "2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalScale.X.Should().Be(2);
        _ = model.ScaleXDiagnostic.Message.Should().BeEmpty();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Vector child-control events traverse the view and group a hundred samples into one entry.</summary>
    /// <param name="field">The environment vector control.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("GroundAlbedo")]
    [DataRow("SkyLuminance")]
    [DataRow("BackgroundColor")]
    public Task EnvironmentVectorControlGroupsSamplesAndUndoRefreshesTheControl(string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView
        {
            ViewModel = model
        };
        var scroller = new ScrollViewer
        {
            Content = view,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var vector = await FindVisibleVectorAsync(view, scroller, field).ConfigureAwait(true);
        var number = vector.FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal))!;
        var before = number.NumberValue;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        for (var sample = 1; sample <= 100; sample++)
        {
            number.NumberValue = sample / 200f;
        }

        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await model.PendingEdits.ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = number.NumberValue.Should().Be(0.5f);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = number.NumberValue.Should().Be(before);
    });
}
