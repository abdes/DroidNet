// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Locates current loaded controls while the inspector virtualizes scrolled content.</summary>
public sealed partial class InspectorControlTests
{
    private static async Task<FrameworkElement> FindInspectorControlAsync(ScrollViewer scroller, Func<FrameworkElement?> resolveControl, string fieldName, CancellationToken cancellationToken)
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
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }

        throw new InvalidOperationException($"Environment field {fieldName} was not realized.");
    }
}
