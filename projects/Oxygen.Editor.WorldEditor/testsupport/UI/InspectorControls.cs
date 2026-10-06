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
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorControls
{
    internal static object FindInspectorElement(UserControl view, string name)
    {
        if (view.FindName(name) is { } parentElement)
        {
            return parentElement;
        }

        if (view is EnvironmentView environment)
        {
            foreach (var section in new[] { "AtmosphereLights", "SkyAtmosphere", "Background", "Exposure", "ToneMapping" })
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

    internal static ToggleButton ComponentButton(SceneNodeEditorView view, Type type) => view.FindDescendant<ToggleButton>(button => button.Tag is InspectorComponentFilter option && option.ComponentType == type)!;

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
                if (control is NumberBox number)
                {
                    _ = number.ApplyTemplate();
                    var label = number.FindDescendant<TextBlock>(element => string.Equals(element.Name, numberPartName, StringComparison.Ordinal));
                    if (label is { IsLoaded: true })
                    {
                        captionEverLoaded = true;
                        var center = label.TransformToVisual(scroller).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
                        var hostCenter = label.TransformToVisual(visual: null).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
                        var captionIsFullyVisible = !string.Equals(numberPartName, "PartLabelTextBlock", StringComparison.Ordinal)
                            || (center.Y >= label.ActualHeight && center.Y <= scroller.ViewportHeight - label.ActualHeight);
                        if (label.ActualWidth > 0 && label.ActualHeight > 0
                            && captionIsFullyVisible && VisualTreeHelper.FindElementsInHostCoordinates(hostCenter, scroller).Contains(label))
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
                            offset = Math.Clamp(scroller.VerticalOffset + center.Y - (scroller.ViewportHeight / 2), 0, scroller.ScrollableHeight);
                            await ScrollInspectorAsync(scroller, offset, cancellationToken).ConfigureAwait(true);
                        }
                    }
                    else
                    {
                        stableFrames = 0;
                        previous = null;
                    }
                }
                else
                {
                    return control;
                }
            }
            else
            {
                stableFrames = 0;
                previous = null;
                offset = offset >= scroller.ScrollableHeight ? 0 : Math.Min(scroller.ScrollableHeight, offset + (scroller.ViewportHeight / 3));
                await ScrollInspectorAsync(scroller, offset, cancellationToken).ConfigureAwait(true);
            }

            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
        }

        var state = lastResolved switch
        {
            null => "was never found in the inspector tree (check that its section is realized)",
            _ when !everLoaded => "was found but never loaded",
            Control { IsEnabled: false } => "was found but IsEnabled=false; a disabled control cannot be typed into, so seed the precondition that enables it",
            NumberBox => captionEverLoaded
                ? $"was realized but its caption never became hit-testable within the ScrollViewer (viewport {scroller.ViewportWidth:0}x{scroller.ViewportHeight:0}, scrollable height {scroller.ScrollableHeight:0})"
                : "was realized but its caption part was never loaded",
            _ => "was realized but its control kind is not returned by this helper",
        };
        throw new InvalidOperationException($"Environment field {fieldName} was not editable: {state}.");
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

    internal static async Task<VectorBox> FindVisibleVectorAsync(EnvironmentView view, ScrollViewer scroller, string field)
    {
        for (var step = 0; step <= 100; step++)
        {
            if (view.FindDescendant<VectorBox>(element => Equals(element.Tag, field)) is { } vector)
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

    internal static async Task PickAssetAsync(SplitButton owner, string name, bool material, CancellationToken cancellationToken)
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
                choice = flyout.Content.FindDescendant<Button>(button => material ? button.DataContext is MaterialPickerRow row && string.Equals(row.Item.Name, name, StringComparison.Ordinal) : button.DataContext is AssetPickerRow asset && string.Equals(asset.Item.Name, name, StringComparison.Ordinal));
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

    internal static bool MatchesInspector(IPropertyEditor<SceneNode> model, string kind) => kind switch
    {
        "Camera" => model is PerspectiveCameraViewModel,
        "Light" => model is DirectionalLightViewModel,
        "Environment" => model is EnvironmentViewModel,
        "Transform" => model is TransformViewModel,
        _ => false,
    };

    internal static UserControl CreateNumericView(IPropertyEditor<SceneNode> model) => model switch
    {
        PerspectiveCameraViewModel camera => new PerspectiveCameraView
        {
            ViewModel = camera,
        },
        DirectionalLightViewModel light => new DirectionalLightView
        {
            ViewModel = light,
        },
        EnvironmentViewModel environment => new EnvironmentView
        {
            ViewModel = environment,
        },
        TransformViewModel transform => new TransformView
        {
            ViewModel = transform,
        },
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    internal static Task PendingNumericEdits(IPropertyEditor<SceneNode> model) => model switch
    {
        TransformViewModel transform => transform.PendingEdits,
        PerspectiveCameraViewModel camera => camera.PendingEdits,
        DirectionalLightViewModel light => light.PendingEdits,
        EnvironmentViewModel environment => environment.PendingEdits,
        _ => throw new ArgumentException("Expected a numeric inspector.", nameof(model)),
    };

    internal static async Task<ComboBox> FindSunPickerAsync(EnvironmentView view, ScrollViewer scroller, EnvironmentViewModel model)
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
