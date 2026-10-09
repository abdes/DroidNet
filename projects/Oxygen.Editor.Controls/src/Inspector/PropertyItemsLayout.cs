// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace Oxygen.Editor.Controls;

/// <summary>Stacks every property item of a <see cref="PropertiesExpander"/> vertically, without virtualization.</summary>
/// <remarks>
/// A section holds a small, fixed set of cards whose heights vary widely. A virtualizing stack layout
/// nested in the inspector's scroll host releases cards outside the viewport and estimates their
/// height, so the scroll extent changes while scrolling and the view jumps back and forth near the
/// end. Keeping every card realized keeps the extent exact.
/// </remarks>
public sealed partial class PropertyItemsLayout : NonVirtualizingLayout
{
    /// <summary>The backing <see cref="DependencyProperty"/> for the <see cref="Spacing"/> property.</summary>
    public static readonly DependencyProperty SpacingProperty = DependencyProperty.Register(
        nameof(Spacing),
        typeof(double),
        typeof(PropertyItemsLayout),
        new PropertyMetadata(0.0, OnSpacingChanged));

    /// <summary>Gets or sets the vertical space between consecutive items.</summary>
    public double Spacing
    {
        get => (double)this.GetValue(SpacingProperty);
        set => this.SetValue(SpacingProperty, value);
    }

    /// <inheritdoc />
    protected override Size MeasureOverride(NonVirtualizingLayoutContext context, Size availableSize)
    {
        var width = 0.0;
        var height = 0.0;
        var children = context.Children;
        for (var index = 0; index < children.Count; ++index)
        {
            var child = children[index];
            child.Measure(new Size(availableSize.Width, double.PositiveInfinity));
            width = Math.Max(width, child.DesiredSize.Width);
            height += child.DesiredSize.Height;
        }

        // Like StackLayout, spacing separates every pair of items.
        height += Math.Max(0, children.Count - 1) * this.Spacing;
        return new Size(width, height);
    }

    /// <inheritdoc />
    protected override Size ArrangeOverride(NonVirtualizingLayoutContext context, Size finalSize)
    {
        var top = 0.0;
        foreach (var child in context.Children)
        {
            child.Arrange(new Rect(0, top, finalSize.Width, child.DesiredSize.Height));
            top += child.DesiredSize.Height + this.Spacing;
        }

        return finalSize;
    }

    private static void OnSpacingChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((PropertyItemsLayout)sender).InvalidateMeasure();
}
