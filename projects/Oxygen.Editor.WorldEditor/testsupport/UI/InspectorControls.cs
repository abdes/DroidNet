// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Mvvm;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class InspectorControls
{
    internal static Type FeedbackType(string kind) => kind switch
    {
        "Camera" => typeof(PerspectiveCamera),
        "Light" => typeof(DirectionalLightComponent),
        _ => typeof(TransformComponent),
    };

    internal static ToggleButton ComponentButton(SceneNodeEditorView view, Type type) => view.FindDescendant<ToggleButton>(button => button.Tag is InspectorComponentFilter option && option.ComponentType == type)!;

    internal static void Toggle(ToggleButton button) => ((IToggleProvider)new ToggleButtonAutomationPeer(button).GetPattern(PatternInterface.Toggle)).Toggle();

    internal static async Task<FrameworkElement> FindInspectorControlAsync(ScrollViewer scroller, Func<FrameworkElement?> resolveControl, string fieldName, CancellationToken cancellationToken)
    {
        var offset = 0d;
        FrameworkElement? previous = null;
        var stableFrames = 0;
        for (var step = 0; step <= 200; step++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var control = resolveControl();
            if (control is { IsLoaded: true })
            {
                if (control is NumberBox number)
                {
                    _ = number.ApplyTemplate();
                    var label = number.FindDescendant<TextBlock>(element => string.Equals(element.Name, "PartValueTextBlock", StringComparison.Ordinal));
                    if (label is { IsLoaded: true })
                    {
                        var center = label.TransformToVisual(scroller).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
                        var hostCenter = label.TransformToVisual(scroller.XamlRoot.Content).TransformPoint(new Point(label.ActualWidth / 2, label.ActualHeight / 2));
                        if (VisualTreeHelper.FindElementsInHostCoordinates(hostCenter, scroller).Contains(label))
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
                            offset = Math.Clamp(scroller.VerticalOffset + center.Y - (scroller.ViewportHeight / 2), 0, scroller.ScrollableHeight);
                            _ = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
                        }
                    }
                }
                else
                {
                    return control;
                }
            }
            else
            {
                offset = offset >= scroller.ScrollableHeight ? 0 : Math.Min(scroller.ScrollableHeight, offset + (scroller.ViewportHeight / 3));
                _ = scroller.ChangeView(horizontalOffset: null, offset, zoomFactor: null, disableAnimation: true);
            }

            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
            {
            }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"Environment field {fieldName} was not realized.");
    }

    internal static async Task EnterTextAsync(NumberBox number, string value)
    {
        _ = number.ApplyTemplate();
        RaiseNumberEvent(number, "StartEdit");
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

    internal static void RaiseNumberEvent(NumberBox number, string method, params object[] arguments)
    {
        var target = typeof(NumberBox).GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)!;
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
                await EnterTextAsync(number, ((float)value).ToString(CultureInfo.CurrentCulture)).ConfigureAwait(true);
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
            ViewModel = camera
        },
        DirectionalLightViewModel light => new DirectionalLightView
        {
            ViewModel = light
        },
        EnvironmentViewModel environment => new EnvironmentView
        {
            ViewModel = environment
        },
        TransformViewModel transform => new TransformView
        {
            ViewModel = transform
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
            if (view.FindDescendant<ComboBox>(element => ReferenceEquals(element.ItemsSource, model.SunOptions)) is { } picker)
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
}
