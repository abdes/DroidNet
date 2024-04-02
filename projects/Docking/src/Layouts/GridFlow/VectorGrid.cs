// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.WinUI.Controls;
using DroidNet.Docking.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class VectorGrid : Grid
{
    private readonly List<bool> fixedSizeItems = [];

    public VectorGrid(Orientation orientation)
    {
        this.Orientation = orientation;

        this.HorizontalAlignment = HorizontalAlignment.Stretch;
        this.VerticalAlignment = VerticalAlignment.Stretch;
    }

    public Orientation Orientation { get; }

    public void AddFixedSizeItem(UIElement content, GridLength length, double minLength)
    {
        this.DefineItem(length, minLength);
        this.AddItem(content);
        this.fixedSizeItems.Add(false);
    }

    public void AddResizableItem(UIElement content, GridLength length, double minLength)
    {
        if (this.Children.Count > 0 && this.fixedSizeItems[this.Children.Count - 1])
        {
            this.AddSplitter();
        }

        this.DefineItem(length, minLength);
        this.AddItem(content);
        this.fixedSizeItems.Add(true);
    }

    public override string ToString() => $"{nameof(VectorGrid)} [{this.Name}]";

    private void DefineItem(GridLength length, double minLength)
    {
        if (this.Orientation == Orientation.Horizontal)
        {
            Debug.WriteLine($"Adding column definition Width={length}");
            this.ColumnDefinitions.Add(
                new ColumnDefinition()
                {
                    Width = length,
                    MinWidth = minLength,
                });
        }
        else
        {
            Debug.WriteLine($"Adding row definition Height={length}");
            this.RowDefinitions.Add(
                new RowDefinition()
                {
                    Height = length,
                    MinHeight = minLength,
                });
        }
    }

    private void AddSplitter()
    {
        var splitter = new DockingSplitter
        {
            Padding = new Thickness(0),
            ResizeBehavior = GridSplitter.GridResizeBehavior.PreviousAndNext,
            ResizeDirection = GridSplitter.GridResizeDirection.Auto,
            Orientation = this.Orientation == Orientation.Horizontal
                ? Orientation.Vertical
                : Orientation.Horizontal,
        };
        this.DefineItem(GridLength.Auto, 6);
        this.AddItem(splitter);
        this.fixedSizeItems.Add(false);
    }

    private void AddItem(UIElement content)
    {
        var (row, column) = this.Orientation == Orientation.Horizontal
            ? (0, this.Children.Count)
            : (this.Children.Count, 0);
        Debug.WriteLine(
            $"Adding item {content} at row={row.ToString(CultureInfo.InvariantCulture)}, col={column.ToString(CultureInfo.InvariantCulture)}");
        content.SetValue(RowProperty, row);
        content.SetValue(ColumnProperty, column);
        this.Children.Add(content);
    }
}
