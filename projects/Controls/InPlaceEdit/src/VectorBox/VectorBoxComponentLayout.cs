// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>Provides internal layout operations for VectorBox component editors.</summary>
internal static class VectorBoxComponentLayout
{
    /// <summary>Gets the default label key for a vector component index.</summary>
    /// <param name="componentIndex">The zero-based component index.</param>
    /// <returns>The axis label.</returns>
    internal static string GetComponentName(int componentIndex)
        => componentIndex switch
        {
            0 => "X",
            1 => "Y",
            2 => "Z",
            _ => throw new ArgumentOutOfRangeException(nameof(componentIndex)),
        };

    /// <summary>Finds the grid that owns a component label and editor.</summary>
    /// <param name="outerGrid">The VectorBox component panel.</param>
    /// <param name="label">The component label.</param>
    /// <param name="box">The component editor.</param>
    /// <returns>The owning grid, if found.</returns>
    internal static Grid? GetComponentContainer(Grid outerGrid, TextBlock label, NumberBox box)
    {
        if (label.Parent is Grid labelContainer)
        {
            return labelContainer;
        }

        if (box.Parent is Grid boxContainer)
        {
            return boxContainer;
        }

        return outerGrid.Children
            .OfType<Grid>()
            .FirstOrDefault(child => child.Children.Contains(label) || child.Children.Contains(box));
    }

    /// <summary>Places an editor without a component label.</summary>
    /// <param name="container">The component container.</param>
    /// <param name="label">The component label to collapse.</param>
    /// <param name="box">The component editor.</param>
    internal static void LayoutWithoutLabel(Grid container, TextBlock label, NumberBox box)
    {
        label.Visibility = Visibility.Collapsed;
        container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetRow(box, 0);
        Grid.SetColumn(box, 0);
    }

    /// <summary>Places a component label beside its editor.</summary>
    /// <param name="container">The component container.</param>
    /// <param name="label">The component label.</param>
    /// <param name="box">The component editor.</param>
    /// <param name="labelOnLeft">Whether the label appears before the editor.</param>
    internal static void LayoutHorizontally(Grid container, TextBlock label, NumberBox box, bool labelOnLeft)
    {
        label.Visibility = Visibility.Visible;
        container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        if (labelOnLeft)
        {
            container.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            Grid.SetColumn(label, 0);
            Grid.SetColumn(box, 1);
        }
        else
        {
            container.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            container.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            Grid.SetColumn(box, 0);
            Grid.SetColumn(label, 1);
        }

        Grid.SetRow(label, 0);
        Grid.SetRow(box, 0);
    }

    /// <summary>Places a component label above or below its editor.</summary>
    /// <param name="container">The component container.</param>
    /// <param name="label">The component label.</param>
    /// <param name="box">The component editor.</param>
    /// <param name="labelOnTop">Whether the label appears above the editor.</param>
    internal static void LayoutVertically(Grid container, TextBlock label, NumberBox box, bool labelOnTop)
    {
        label.Visibility = Visibility.Visible;
        container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        container.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        label.HorizontalTextAlignment = TextAlignment.Center;
        label.HorizontalAlignment = HorizontalAlignment.Stretch;

        if (labelOnTop)
        {
            Grid.SetRow(label, 0);
            Grid.SetRow(box, 1);
        }
        else
        {
            Grid.SetRow(box, 0);
            Grid.SetRow(label, 1);
        }

        Grid.SetColumn(box, 0);
        Grid.SetColumn(label, 0);
    }
}
