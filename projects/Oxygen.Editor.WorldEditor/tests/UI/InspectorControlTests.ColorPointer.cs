// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Color = Windows.UI.Color;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises actual pointer capture and release in the WinUI color spectrum.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Gets or sets the context for cancellation of the running test.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Real spectrum input commits once on release and cancels only an interrupted drag.</summary>
    /// <param name="kind">The inspector whose picker is exercised.</param>
    /// <param name="drag">Whether to move while the button is held.</param>
    /// <param name="cancel">Whether to interrupt capture while the button remains held.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Environment", false, false)]
    [DataRow("Environment", true, false)]
    [DataRow("Environment", true, true)]
    [DataRow("Light", false, false)]
    [DataRow("Light", true, false)]
    [DataRow("Light", true, true)]
    public Task InspectorSpectrumPointerCompletionPreservesHistory(string kind, bool drag, bool cancel) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var model = CreateModel(kind, fixture);
        var view = await CreateColorViewAsync(model).ConfigureAwait(true);
        fixture.Context.History.Clear();
        var flyout = await OpenColorFlyoutAsync(view).ConfigureAwait(true);
        var before = ReadSceneColor(fixture, kind);
        try
        {
            var preview = await DriveSpectrumAsync((ColorPicker)flyout.Content, drag, cancel).ConfigureAwait(true);
            await PendingColorEdits(model).ConfigureAwait(true);
            var expected = cancel ? before : new Vector3(preview.R / 255f, preview.G / 255f, preview.B / 255f);
            _ = ReadSceneColor(fixture, kind).Should().Be(expected);
            _ = fixture.Context.History.UndoStack.Should().HaveCount(cancel ? 0 : 1);
            if (!cancel)
            {
                await fixture.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
                _ = ReadSceneColor(fixture, kind).Should().Be(before);
                await fixture.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
                _ = ReadSceneColor(fixture, kind).Should().Be(expected);
            }
        }
        finally
        {
            await CloseColorFlyoutAsync(flyout).ConfigureAwait(true);
        }
    });

    private static Vector3 ReadSceneColor(Fixture fixture, string kind) => string.Equals(kind, "Environment", StringComparison.Ordinal)
        ? fixture.Scene.Environment.BackgroundColor : fixture.Node.Components.OfType<DirectionalLightComponent>().Single().Color;

    private static Task PendingColorEdits(IDisposable model) => model is EnvironmentViewModel environment ? environment.PendingEdits : ((DirectionalLightViewModel)model).PendingEdits;

    private static async Task<UserControl> CreateColorViewAsync(IDisposable model)
    {
        if (model is EnvironmentViewModel environment)
        {
            environment.SetBackgroundColor(Microsoft.UI.Colors.White);
            await environment.PendingEdits.ConfigureAwait(true);
            return new EnvironmentView { ViewModel = environment };
        }

        return new DirectionalLightView { ViewModel = (DirectionalLightViewModel)model };
    }

    private static async Task<Flyout> OpenColorFlyoutAsync(UserControl view)
    {
        ScrollViewer scroller;
        if (view is Oxygen.Editor.MaterialEditor.MaterialEditorView)
        {
            await LoadTestContentAsync(view).ConfigureAwait(true);
            scroller = view.FindDescendant<ScrollViewer>()!;
        }
        else
        {
            scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
            await LoadTestContentAsync(scroller).ConfigureAwait(true);
        }

        var button = await FindVisiblePickerButtonAsync(view, scroller).ConfigureAwait(true);
        button.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var flyout = (Flyout)button.Flyout;
        var picker = (ColorPicker)flyout.Content;
        var loaded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnLoaded(object sender, RoutedEventArgs args) => loaded.TrySetResult();
        picker.Loaded += OnLoaded;
        flyout.ShowAt(button);
        await loaded.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        picker.Loaded -= OnLoaded;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        return flyout;
    }

    private static async Task<Button> FindVisiblePickerButtonAsync(UserControl view, ScrollViewer scroller)
    {
        for (var step = 0; step <= 100; step++)
        {
            if (view.FindDescendants().OfType<Button>().SingleOrDefault(value => value.Flyout is Flyout { Content: ColorPicker }) is { } button)
            {
                return button;
            }

            var offset = Math.Min(scroller.ScrollableHeight, (step + 1) * scroller.ViewportHeight / 2);
            _ = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        throw new InvalidOperationException("The color-picker button was not realized while scrolling its view.");
    }

    private static async Task CloseColorFlyoutAsync(Flyout flyout)
    {
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Closed += OnClosed;
        flyout.Hide();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        flyout.Closed -= OnClosed;
    }

    private static async Task<Color> DriveSpectrumAsync(ColorPicker picker, bool drag, bool cancel)
    {
        using var pointer = PointerInput.Capture();
        var spectrum = picker.FindDescendant<ColorSpectrum>()!;
        var captures = new List<bool>();
        PointerEventHandler captureLost = (_, args) => captures.Add(args.Pointer.IsInContact);
        picker.AddHandler(UIElement.PointerCaptureLostEvent, captureLost, handledEventsToo: true);
        var before = picker.Color;
        try
        {
            await PointerInput.MoveAsync(spectrum, 0.25, 0.25).ConfigureAwait(true);
            await PointerInput.ButtonAsync(down: true).ConfigureAwait(true);
            if (drag)
            {
                await PointerInput.MoveAsync(spectrum, 0.5, 0.5).ConfigureAwait(true);
                await PointerInput.MoveAsync(spectrum, 0.75, 0.75).ConfigureAwait(true);
            }

            var preview = picker.Color;
            _ = preview.Should().NotBe(before, "the real pointer must change the spectrum");
            if (cancel)
            {
                spectrum.FindDescendant<FrameworkElement>(element => string.Equals(element.Name, "InputTarget", StringComparison.Ordinal))!.ReleasePointerCaptures();
            }

            await PointerInput.ButtonAsync(down: false).ConfigureAwait(true);
            _ = captures.Should().ContainSingle().Which.Should().Be(cancel);
            _ = picker.Color.Should().Be(cancel ? before : preview);
            return preview;
        }
        finally
        {
            await PointerInput.ButtonAsync(down: false).ConfigureAwait(true);
            picker.RemoveHandler(UIElement.PointerCaptureLostEvent, captureLost);
        }
    }
}
