// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using CommunityToolkit.WinUI.Controls;
using DroidNet.Routing.Debugger.UI.Styles;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class VectorGrid : Grid
{
    private readonly Orientation orientation;

    public VectorGrid(Orientation orientation)
    {
        this.orientation = orientation;

        this.HorizontalAlignment = HorizontalAlignment.Stretch;
        this.VerticalAlignment = VerticalAlignment.Stretch;
    }

    public void DefineItem(GridLength length)
    {
        if (this.orientation == Orientation.Horizontal)
        {
            this.ColumnDefinitions.Add(new ColumnDefinition() { Width = length });
        }
        else
        {
            this.RowDefinitions.Add(new RowDefinition() { Height = length });
        }
    }

    public void AddSplitter(int index)
    {
        var splitter = new DockingSplitter
        {
            Padding = new Thickness(0),
            ResizeBehavior = GridSplitter.GridResizeBehavior.PreviousAndNext,
            ResizeDirection = GridSplitter.GridResizeDirection.Auto,
            Orientation = this.orientation == Orientation.Horizontal
                ? Orientation.Vertical
                : Orientation.Horizontal,
        };
        this.AddItem(splitter, index);
    }

    public void AddItem(UIElement content, int index)
    {
        var (row, column) = this.orientation == Orientation.Horizontal ? (0, index) : (index, 0);
        content.SetValue(RowProperty, row);
        content.SetValue(ColumnProperty, column);
        this.Children.Add(content);
    }
}
