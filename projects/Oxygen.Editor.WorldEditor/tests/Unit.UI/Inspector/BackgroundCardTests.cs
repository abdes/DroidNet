// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using PropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

public sealed partial class InspectorBindingTests
{
    [TestMethod]
    public Task ExposureCurvePreviewUsesViewOwnedPointsAndTracksUndo() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        model.ExposureMode = ExposureMode.Auto;
        await model.PendingEdits.ConfigureAwait(true);
        var view = new EnvironmentView { ViewModel = model, Width = 420, Height = 650 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "Exposure-compensation";
        var section = (PropertiesExpander)view.FindName("ExposureSection");
        var disclosure = section.Items.OfType<Expander>().Single(item => string.Equals(item.Header as string, "Exposure shaping", StringComparison.Ordinal));
        disclosure.IsExpanded = true;
        _ = section.BringItemIntoView(disclosure);
        await WaitForRenderAsync().ConfigureAwait(true);
        var preview = view.FindDescendants().OfType<Microsoft.UI.Xaml.Shapes.Polyline>().Single();
        var original = preview.Points.Select(point => (point.X, point.Y)).ToArray();

        model.AutoExposureCompensationCurve =
        [
            new ExposureCompensationKeyData(-2, -1),
            new ExposureCompensationKeyData(0, 1),
            new ExposureCompensationKeyData(2, 0),
        ];
        await model.PendingEdits.ConfigureAwait(true);

        _ = preview.Points.Select(point => (point.X, point.Y)).Should().Equal((0d, 32d), (80d, 4d), (160d, 18d));
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = preview.Points.Select(point => (point.X, point.Y)).Should().Equal(original);
    });

    [TestMethod]
    public Task BackgroundResetIsUndoableAndPreservesOtherSceneSettings() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "background";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

        model.SetBackgroundColor(InspectorRgbPresentation.ToLinearRgb(Windows.UI.Color.FromArgb(255, 128, 64, 32)));
        await model.PendingEdits.ConfigureAwait(true);
        var original = fixture.Scene.Environment;
        var count = fixture.Context.History.UndoStack.Count;
        var reset = (Button)view.FindName("ResetBackgroundButton");
        ((IInvokeProvider)new ButtonAutomationPeer(reset).GetPattern(PatternInterface.Invoke)).Invoke();
        await model.PendingEdits.ConfigureAwait(true);

        _ = fixture.Scene.Environment.BackgroundColor.Should().Be(Vector3.Zero);
        _ = fixture.Scene.Environment.Should().Be(original with { BackgroundColor = Vector3.Zero });
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
        _ = fixture.Context.History.UndoStack.Should().HaveCount(count + 1);
        _ = model.BackgroundR.Should().Be(0);
        _ = model.BackgroundG.Should().Be(0);
        _ = model.BackgroundB.Should().Be(0);

        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = fixture.Scene.Environment.Should().Be(original);
        _ = new Vector3(model.BackgroundR, model.BackgroundG, model.BackgroundB).Should().Be(original.BackgroundColor);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = fixture.Scene.Environment.BackgroundColor.Should().Be(Vector3.Zero);
    });

    [TestMethod]
    public Task BackgroundPickerAndLinearChannelsKeepDisplaySwatchInSync() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = (EnvironmentViewModel)CreateModel("Environment", fixture);
        var view = new EnvironmentView { ViewModel = model };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        ((TextBox)view.FindName("ScenePropertySearchBox")).Text = "background";
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var swatch = view.FindDescendant<Button>(button => button.Name == "BackgroundSwatch")!;
        var displayColor = Windows.UI.Color.FromArgb(255, 128, 64, 32);
        await PickDisplayColorAsync(swatch, displayColor).ConfigureAwait(true);
        await model.PendingEdits.ConfigureAwait(true);

        _ = model.BackgroundR.Should().BeApproximately(0.21586f, 0.00001f);
        _ = model.BackgroundG.Should().BeApproximately(0.05127f, 0.00001f);
        _ = model.BackgroundB.Should().BeApproximately(0.01444f, 0.00001f);
        _ = model.BackgroundColor.Should().Be(fixture.Scene.Environment.BackgroundColor);
        _ = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color.Should().Be(displayColor);
        model.BackgroundR = 1;
        await model.PendingEdits.ConfigureAwait(true);
        var display = ((Microsoft.UI.Xaml.Media.SolidColorBrush)((Border)swatch.Content).Background).Color;
        _ = display.R.Should().Be(255);
        _ = display.G.Should().Be(64);
        _ = display.B.Should().Be(32);
        _ = fixture.Scene.Environment.BackgroundColor.Should().Be(new Vector3(1, model.BackgroundG, model.BackgroundB));
    });

    private static async Task PickDisplayColorAsync(Button swatch, Windows.UI.Color color)
    {
        var flyout = (Flyout)swatch.Flyout;
        var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnOpened(object? sender, object args) => opened.TrySetResult();
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Opened += OnOpened;
        flyout.Closed += OnClosed;
        try
        {
            flyout.ShowAt(swatch);
            await opened.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            ((ColorPicker)flyout.Content).Color = color;
            flyout.Hide();
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
        }
        finally
        {
            flyout.Opened -= OnOpened;
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }
}
