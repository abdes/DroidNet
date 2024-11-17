// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI.Controls;
using DroidNet.Docking.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Docking.Layouts;

/// <summary>
/// A one-dimensional grid (vector) which can be oriented horizontally or vertically and can hold fixed-size or resizable items.
/// </summary>
/// <remarks>
/// The <see cref="ResizableVectorGrid"/> class extends the <see cref="Grid"/> class to provide a flexible layout container that can hold both fixed-size and resizable items. It supports both horizontal and vertical orientations.
/// </remarks>
public partial class ResizableVectorGrid : Grid
{
    private readonly List<bool> resizableItems = [];

    private bool containsStretchToFillItem;

    /// <summary>
    /// Initializes a new instance of the <see cref="ResizableVectorGrid"/> class with the specified orientation.
    /// </summary>
    /// <param name="orientation">The orientation of the grid, either horizontal or vertical.</param>
    /// <remarks>
    /// This constructor sets the orientation of the grid and initializes its alignment properties.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var grid = new ResizableVectorGrid(Orientation.Horizontal);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public ResizableVectorGrid(Orientation orientation)
    {
        this.VectorOrientation = orientation;

        this.HorizontalAlignment = HorizontalAlignment.Stretch;
        this.VerticalAlignment = VerticalAlignment.Stretch;
    }

    /// <summary>
    /// Gets the orientation of the vector grid.
    /// </summary>
    /// <value>
    /// The orientation of the grid, either horizontal or vertical.
    /// </value>
    /// <remarks>
    /// This property indicates whether the grid is oriented horizontally or vertically.
    /// </remarks>
    public Orientation VectorOrientation { get; }

    /// <summary>
    /// Adds a fixed-size item to the grid.
    /// </summary>
    /// <param name="content">The content to add to the grid.</param>
    /// <param name="length">The length of the item.</param>
    /// <param name="minLength">The minimum length of the item.</param>
    /// <remarks>
    /// This method adds a fixed-size item to the grid with the specified length and minimum length.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var grid = new ResizableVectorGrid(Orientation.Horizontal);
    /// grid.AddFixedSizeItem(new Button { Content = "Fixed" }, new GridLength(100), 50);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public void AddFixedSizeItem(UIElement content, GridLength length, double minLength)
    {
        this.DefineItem(length, minLength);
        this.AddItem(content);
        this.resizableItems.Add(false);
    }

    /// <summary>
    /// Adds a resizable item to the grid.
    /// </summary>
    /// <param name="content">The content to add to the grid.</param>
    /// <param name="length">The length of the item.</param>
    /// <param name="minLength">The minimum length of the item.</param>
    /// <remarks>
    /// This method adds a resizable item to the grid with the specified length and minimum length. If the length is a star value, the item will stretch to fill the available space.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var grid = new ResizableVectorGrid(Orientation.Horizontal);
    /// grid.AddResizableItem(new Button { Content = "Resizable" }, new GridLength(1, GridUnitType.Star), 50);
    /// ]]></code>
    /// </para>
    /// </remarks>
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

    /// <inheritdoc/>
    public override string ToString() => $"{nameof(ResizableVectorGrid)} [{this.Name}]";

    /// <summary>
    /// Adjusts the sizes of the resizable items in the grid.
    /// </summary>
    /// <remarks>
    /// This method adjusts the sizes of the resizable items in the grid. If the grid contains a stretch-to-fill item, no adjustments are made.
    /// </remarks>
    public void AdjustSizes()
    {
        if (this.containsStretchToFillItem)
        {
            return;
        }

        if (this.VectorOrientation == Orientation.Horizontal)
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
        if (this.VectorOrientation == Orientation.Horizontal)
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
            Orientation = this.VectorOrientation == Orientation.Horizontal
                ? Orientation.Vertical
                : Orientation.Horizontal,
        };
        this.DefineItem(GridLength.Auto, 6);
        this.AddItem(splitter);
        this.resizableItems.Add(false);
    }

    private void AddItem(UIElement content)
    {
        var (row, column) = this.VectorOrientation == Orientation.Horizontal
            ? (0, this.Children.Count)
            : (this.Children.Count, 0);

        // $"Adding item {content} at row={row.ToString(CultureInfo.InvariantCulture)}, col={column.ToString(CultureInfo.InvariantCulture)}"
        content.SetValue(RowProperty, row);
        content.SetValue(ColumnProperty, column);
        this.Children.Add(content);
    }
}
