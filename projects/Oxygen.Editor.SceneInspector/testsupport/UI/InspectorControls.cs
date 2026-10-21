// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.TestSupport;

internal static class InspectorControls
{
    internal static object FindInspectorElement(UserControl view, string name)
    {
        if (view.FindName(name) is { } parentElement)
        {
            return parentElement;
        }

        if (view is SceneSettingsView environment)
        {
            foreach (var section in new[] { "AtmosphereLights", "Backdrop", "SkyAtmosphere", "SkyLight", "Fog", "Exposure", "ToneMapping" })
            {
                if (environment.SectionView(section).FindName(name) is { } element)
                {
                    return element;
                }
            }
        }

        throw new InvalidOperationException($"Inspector element {name} was not found in its owning namescope.");
    }

    internal static Type FeedbackType(string kind) => kind switch
    {
        "Camera" => typeof(PerspectiveCamera),
        "Light" => typeof(DirectionalLightComponent),
        _ => typeof(TransformComponent),
    };

    internal static ToggleButton ComponentButton(SceneInspectorView view, Type type) => view.FindDescendant<ToggleButton>(button => button.Tag is InspectorComponentFilter option && option.ComponentType == type)!;

    internal static void Toggle(ToggleButton button) => ((IToggleProvider)new ToggleButtonAutomationPeer(button).GetPattern(PatternInterface.Toggle)).Toggle();

    internal static async Task<FrameworkElement> FindInspectorControlAsync(ScrollViewer scroller, Func<FrameworkElement?> resolveControl, string fieldName, CancellationToken cancellationToken, string numberPartName = "PartValueTextBlock")
    {
        var offset = 0d;
        FrameworkElement? previous = null;
        var stableFrames = 0;
        FrameworkElement? lastResolved = null;
        var everLoaded = false;
        var captionEverLoaded = false;
        for (var step = 0; step <= 200; step++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var control = resolveControl();
            scroller.UpdateLayout();
            lastResolved ??= control;
            if (control is { IsLoaded: true })
            {
                everLoaded = true;
                if (control is not NumberBox number)
                {
                    return control;
                }

                _ = number.ApplyTemplate();
                var label = number.FindDescendant<TextBlock>(element => string.Equals(element.Name, numberPartName, StringComparison.Ordinal));
                captionEverLoaded |= label is { IsLoaded: true };
                if (label is { IsLoaded: true } && IsCaptionHitTestable(label, scroller, numberPartName))
                {
                    stableFrames = ReferenceEquals(previous, number) ? stableFrames + 1 : 0;
                    previous = number;
                    if (stableFrames >= 3)
                    {
                        return number;
                    }
                }
                else
                {
                    stableFrames = 0;
                    previous = null;
                    if (label is { IsLoaded: true })
                    {
                        offset = CenteredOffset(label, scroller);
                        await ScrollInspectorAsync(scroller, offset, cancellationToken).ConfigureAwait(true);
                    }
                }
            }
            else
            {
                stableFrames = 0;
                previous = null;
                offset = offset >= scroller.ScrollableHeight ? 0 : Math.Min(scroller.ScrollableHeight, offset + (scroller.ViewportHeight / 3));
                await ScrollInspectorAsync(scroller, offset, cancellationToken).ConfigureAwait(true);
            }

            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"Environment field {fieldName} was not editable: {NotEditableReason(lastResolved, everLoaded, captionEverLoaded, scroller)}.");
    }

    internal static async Task EnterTextAsync(NumberBox number, string value)
    {
        _ = number.ApplyTemplate();
        number.StartEdit();
        var input = number.FindDescendant<TextBox>(element => string.Equals(element.Name, "PartEditBox", StringComparison.Ordinal))!;
        if (string.Equals(input.Text, value, StringComparison.Ordinal))
        {
            return;
        }

        var changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void TextChanged(object sender, TextChangedEventArgs args)
        {
            if (string.Equals(input.Text, value, StringComparison.Ordinal))
            {
                _ = changed.TrySetResult();
            }
        }

        input.TextChanged += TextChanged;
        try
        {
            input.Text = value;
            await changed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        }
        finally
        {
            input.TextChanged -= TextChanged;
        }
    }

    internal static async Task<VectorBox> FindVisibleVectorAsync(SceneSettingsView view, ScrollViewer scroller, string field)
    {
        for (var step = 0; step <= 100; step++)
        {
            if (view.FindDescendant<Oxygen.Editor.Controls.InspectorColorField>(element => Equals(element.Tag, field))?.FindDescendant<VectorBox>() is { } vector)
            {
                return vector;
            }

            var offset = Math.Min(scroller.ScrollableHeight, (step + 1) * scroller.ViewportHeight / 2);
            _ = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"The environment vector '{field}' was not realized while scrolling its inspector.");
    }

    internal static async Task<T> WaitForDescendantAsync<T>(DependencyObject root, Func<T, bool> matches, string description, CancellationToken cancellationToken)
        where T : FrameworkElement
    {
        for (var frame = 0; frame <= 200; frame++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (root.FindDescendant(matches) is { } found)
            {
                return found;
            }

            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"The {description} was not realized in the inspector tree within 200 render passes.");
    }

    internal static void RaiseNumberEvent(NumberBox number, string method, params object[] arguments)
    {
        var target = typeof(NumberBox).GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException($"DroidNet.Controls.NumberBox does not declare an instance method '{method}'.");
        var parameters = target.GetParameters();
        var missingParameters = parameters.Skip(arguments.Length).ToArray();
        if (arguments.Length > parameters.Length || missingParameters.Any(static parameter => !parameter.IsOptional))
        {
            throw new TargetParameterCountException($"Method {method} does not accept {arguments.Length} arguments.");
        }

        var invocationArguments = arguments.Concat(missingParameters.Select(static parameter => parameter.DefaultValue)).ToArray();
        _ = target.Invoke(number, invocationArguments);
    }

    internal static async Task SetEnvironmentControlValueAsync(FrameworkElement control, object value)
    {
        switch (control)
        {
            case NumberBox number:
                var displayValue = (float)value;
                if (Equals(number.Tag, "AngularSizeRadians"))
                {
                    displayValue *= 180f / MathF.PI;
                }

                await EnterTextAsync(number, displayValue.ToString(CultureInfo.CurrentCulture)).ConfigureAwait(true);
                number.CompletePendingTextEdit();
                break;
            case ToggleSwitch toggle:
                toggle.IsOn = (bool)value;
                break;
            case CheckBox check:
                check.IsChecked = (bool)value;
                break;
            case ComboBox combo:
                combo.SelectedItem = value;
                break;
            default:
                throw new InvalidOperationException($"Unsupported environment control {control.GetType().Name}.");
        }
    }

    internal static async Task PickAssetAsync(SplitButton owner, string name, CancellationToken cancellationToken)
    {
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Closed += OnClosed;
        try
        {
            flyout.ShowAt(owner);
            Button? choice = null;
            while (choice is null)
            {
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
                {
                }).ConfigureAwait(true);
                cancellationToken.ThrowIfCancellationRequested();
                choice = flyout.Content.FindDescendant<Button>(button => string.Equals(AutomationProperties.GetName(button), name, StringComparison.Ordinal));
            }

            _ = choice.IsEnabled.Should().BeTrue();
            ((IInvokeProvider)new ButtonAutomationPeer(choice).GetPattern(PatternInterface.Invoke)).Invoke();
            await closed.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }

    // A field's row card; its feedback text sits beside the card, below it.
    internal static Oxygen.Editor.Controls.PropertyCard CardOf(UserControl field)
        => ((Panel)field.Content).Children.OfType<Oxygen.Editor.Controls.PropertyCard>().Single();

    internal static Button SwatchOf(Oxygen.Editor.Controls.InspectorColorField field)
        => field.FindDescendant<Button>(button => string.Equals(button.Name, "PartSwatchButton", StringComparison.Ordinal))!;

    internal static Windows.UI.Color SwatchColor(Oxygen.Editor.Controls.InspectorColorField field)
        => ((SolidColorBrush)field.FindDescendant<Border>(border => string.Equals(border.Name, "PartSwatchFill", StringComparison.Ordinal))!.Background).Color;

    internal static async Task PickDisplayColorAsync(Button swatch, Windows.UI.Color color)
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
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }
        finally
        {
            flyout.Opened -= OnOpened;
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }

    internal static bool MatchesInspector(InspectorSection model, string kind) => kind switch
    {
        "Camera" => model is PerspectiveCameraViewModel,
        "Light" => model is DirectionalLightViewModel,
        "Environment" => model is SceneSettingsViewModel,
        "Transform" => model is TransformViewModel,
        _ => false,
    };

    internal static UserControl CreateNumericView(InspectorSection model) => model switch
    {
        PerspectiveCameraViewModel camera => new PerspectiveCameraView
        {
            ViewModel = camera,
        },
        DirectionalLightViewModel light => new DirectionalLightView
        {
            ViewModel = light,
        },
        SceneSettingsViewModel environment => new SceneSettingsView
        {
            ViewModel = environment,
        },
        TransformViewModel transform => new TransformView
        {
            ViewModel = transform,
        },
        OrthographicCameraViewModel orthographic => new OrthographicCameraView
        {
            ViewModel = orthographic,
        },
        PointLightViewModel point => new PointLightView
        {
            ViewModel = point,
        },
        SpotLightViewModel spot => new SpotLightView
        {
            ViewModel = spot,
        },
        NodeRenderingViewModel rendering => new NodeRenderingView
        {
            ViewModel = rendering,
        },
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    internal static Task PendingNumericEdits(InspectorSection model) => model switch
    {
        TransformViewModel transform => transform.PendingEdits,
        PerspectiveCameraViewModel camera => camera.PendingEdits,
        DirectionalLightViewModel light => light.PendingEdits,
        SceneSettingsViewModel environment => environment.PendingEdits,
        OrthographicCameraViewModel orthographic => orthographic.PendingEdits,
        LocalLightViewModel light => light.PendingEdits,
        NodeRenderingViewModel rendering => rendering.PendingEdits,
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    internal static async Task<ComboBox> FindSunPickerAsync(SceneSettingsView view, ScrollViewer scroller, SceneSettingsViewModel model)
    {
        for (var step = 0; step <= 40; step++)
        {
            if (view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.AtmosphereLights.SunOptions)) is { } picker)
            {
                return picker;
            }

            _ = scroller.ChangeView(horizontalOffset: null, Math.Min(scroller.ScrollableHeight, (step + 1) * scroller.ViewportHeight / 2), zoomFactor: null, disableAnimation: true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
        }

        throw new InvalidOperationException("The sun picker was not realized in the environment inspector.");
    }

    // A caption counts only when it is laid out, inside the viewport (fully, for an outside label) and on top.
    private static bool IsCaptionHitTestable(TextBlock label, ScrollViewer scroller, string numberPartName)
    {
        var center = label.TransformToVisual(scroller).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
        var hostCenter = label.TransformToVisual(visual: null).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
        var captionIsFullyVisible = !string.Equals(numberPartName, "PartLabelTextBlock", StringComparison.Ordinal)
            || (center.Y >= label.ActualHeight && center.Y <= scroller.ViewportHeight - label.ActualHeight);
        return label.ActualWidth > 0 && label.ActualHeight > 0
            && captionIsFullyVisible && VisualTreeHelper.FindElementsInHostCoordinates(hostCenter, scroller).Contains(label);
    }

    // The scroll offset that brings the caption to the middle of the viewport.
    private static double CenteredOffset(TextBlock label, ScrollViewer scroller)
    {
        var center = label.TransformToVisual(scroller).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
        return Math.Clamp(scroller.VerticalOffset + center.Y - (scroller.ViewportHeight / 2), 0, scroller.ScrollableHeight);
    }

    private static string NotEditableReason(FrameworkElement? lastResolved, bool everLoaded, bool captionEverLoaded, ScrollViewer scroller) => lastResolved switch
    {
        null => "was never found in the inspector tree (check that its section is realized)",
        _ when !everLoaded => "was found but never loaded",
        Control { IsEnabled: false } => "was found but IsEnabled=false; a disabled control cannot be typed into, so seed the precondition that enables it",
        NumberBox => captionEverLoaded
            ? $"was realized but its caption never became hit-testable within the ScrollViewer (viewport {scroller.ViewportWidth:0}x{scroller.ViewportHeight:0}, scrollable height {scroller.ScrollableHeight:0})"
            : "was realized but its caption part was never loaded",
        _ => "was realized but its control kind is not returned by this helper",
    };

    private static async Task ScrollInspectorAsync(ScrollViewer scroller, double offset, CancellationToken cancellationToken)
    {
        var reachableOffset = Math.Clamp(offset, 0, scroller.ScrollableHeight);
        if (Math.Abs(scroller.VerticalOffset - reachableOffset) <= 0.5)
        {
            return;
        }

        var viewCompleted = false;
        void ViewChanged(object? sender, ScrollViewerViewChangedEventArgs args) => viewCompleted = !args.IsIntermediate;
        scroller.ViewChanged += ViewChanged;
        try
        {
            var accepted = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
            var previousOffset = scroller.VerticalOffset;
            var previousHeight = scroller.ScrollableHeight;
            var stableFrames = 0;
            for (var frame = 0; frame < 20; frame++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
                scroller.UpdateLayout();
                stableFrames = Math.Abs(scroller.VerticalOffset - previousOffset) <= 0.5
                    && Math.Abs(scroller.ScrollableHeight - previousHeight) <= 0.5 ? stableFrames + 1 : 0;
                previousOffset = scroller.VerticalOffset;
                previousHeight = scroller.ScrollableHeight;

                // Scroll anchoring can adjust the requested offset while the content lays out.
                if ((!accepted || viewCompleted) && stableFrames >= 2)
                {
                    return;
                }
            }
        }
        finally
        {
            scroller.ViewChanged -= ViewChanged;
        }

        throw new InvalidOperationException(
            $"Inspector scroll did not settle at {offset:0.##}; actual offset {scroller.VerticalOffset:0.##}, scrollable height {scroller.ScrollableHeight:0.##}.");
    }
}
