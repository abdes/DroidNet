// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts;

using CommunityToolkit.WinUI.Controls;
using DroidNet.Docking.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class ResizableVectorGrid : Grid
{
    private readonly List<bool> resizableItems = [];

    private bool containsStretchToFillItem;

    public ResizableVectorGrid(Orientation orientation)
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
        this.resizableItems.Add(false);
    }

    public void AddResizableItem(UIElement content, GridLength length, double minLength)
    {
        if (length.IsStar)
        {
            this.containsStretchToFillItem = true;
        }

        if (this.Children.Count > 0 && this.resizableItems[this.Children.Count - 1])
        {
            this.AddSplitter();
        }

        this.DefineItem(length, minLength);
        this.AddItem(content);
        this.resizableItems.Add(true);
    }

    public override string ToString() => $"{nameof(ResizableVectorGrid)} [{this.Name}]";

    public void AdjustSizes()
    {
        if (this.containsStretchToFillItem)
        {
            return;
        }

        if (this.Orientation == this.Orientation.Horizontal)
        {
            this.AdjustColumnWidths();
        }
        else
        {
            this.AdjustRowHeights();
        }
    }

    private void AdjustRowHeights()
    {
        for (var index = 0; index < this.RowDefinitions.Count; index++)
        {
            if (this.resizableItems[index])
            {
                var row = this.RowDefinitions[index];
                if (row.Height.IsAbsolute)
                {
                    row.Height = new GridLength(row.Height.Value, GridUnitType.Star);
                }
            }
        }
    }

    private void AdjustColumnWidths()
    {
        for (var index = 0; index < this.ColumnDefinitions.Count; index++)
        {
            if (this.resizableItems[index])
            {
                var column = this.ColumnDefinitions[index];
                if (column.Width.IsAbsolute)
                {
                    column.Width = new GridLength(column.Width.Value, GridUnitType.Star);
                }
            }
        }
    }

    private void DefineItem(GridLength length, double minLength)
    {
        if (this.Orientation == this.Orientation.Horizontal)
        {
            // $"Adding column definition Width={length}"
            this.ColumnDefinitions.Add(
                new ColumnDefinition()
                {
                    Width = length,
                    MinWidth = minLength,
                });
        }
        else
        {
            // $"Adding row definition Height={length}"
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
            Orientation = this.Orientation == this.Orientation.Horizontal
                ? this.Orientation.Vertical
                : this.Orientation.Horizontal,
        };
        this.DefineItem(GridLength.Auto, 6);
        this.AddItem(splitter);
        this.resizableItems.Add(false);
    }

    private void AddItem(UIElement content)
    {
        var (row, column) = this.Orientation == this.Orientation.Horizontal
            ? (0, this.Children.Count)
            : (this.Children.Count, 0);

        // $"Adding item {content} at row={row.ToString(CultureInfo.InvariantCulture)}, col={column.ToString(CultureInfo.InvariantCulture)}"
        content.SetValue(RowProperty, row);
        content.SetValue(ColumnProperty, column);
        this.Children.Add(content);
    }
}
