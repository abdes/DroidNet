// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>Overlays numeric text editing without letting the text editor change the value's layout footprint.</summary>
public sealed partial class NumberBoxValuePanel : Panel
{
    /// <inheritdoc />
    protected override Size MeasureOverride(Size availableSize)
    {
        if (this.Children.Count != 2)
        {
            throw new InvalidOperationException("NumberBoxValuePanel requires a display value followed by its text editor.");
        }

        var display = this.Children[0];
        display.Measure(availableSize);
        var width = double.IsPositiveInfinity(availableSize.Width) ? display.DesiredSize.Width : availableSize.Width;
        this.Children[1].Measure(new Size(width, display.DesiredSize.Height));
        return display.DesiredSize;
    }

    /// <inheritdoc />
    protected override Size ArrangeOverride(Size finalSize)
    {
        var bounds = new Rect(default, finalSize);
        foreach (var child in this.Children)
        {
            child.Arrange(bounds);
        }

        return finalSize;
    }
}
