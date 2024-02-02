// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using CommunityToolkit.WinUI.Controls;
using DroidNet.Docking;
using DroidNet.Routing.Debugger.UI.Styles;
using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Orientation = Microsoft.UI.Xaml.Controls.Orientation;

[ViewModel(typeof(WorkSpaceViewModel))]
public partial class WorkSpaceView : ContentControl
{
    public WorkSpaceView()
    {
        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        this.ViewModelChanged += (_, _) => this.Content = this.GridForDockGroup(this.ViewModel.Root);
    }

    private static void AddSplitter(Grid grid, int row, int column)
    {
        var splitter = new DockingSplitter()
        {
            Orientation = row == 0 ? Orientation.Vertical : Orientation.Horizontal,
            Padding = new Thickness(0),
            ResizeBehavior = GridSplitter.GridResizeBehavior.PreviousAndNext,
            ResizeDirection = GridSplitter.GridResizeDirection.Auto,
        };
        splitter.SetValue(Grid.RowProperty, row);
        splitter.SetValue(Grid.ColumnProperty, column);
        grid.Children.Add(splitter);
    }

    private static void DefineSlot(Grid grid, GridLength partLength, bool useColumns)
    {
        if (useColumns)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition() { Width = partLength });
        }
        else
        {
            grid.RowDefinitions.Add(new RowDefinition() { Height = partLength });
        }
    }

    private static void DefineSlots(IDockGroup group, Grid grid, GridLength partLength, bool useColumns)
    {
        if (group.First != null)
        {
            DefineSlot(grid, partLength, useColumns);
        }

        if (group.Second == null)
        {
            return;
        }

        if (group.First != null)
        {
            DefineSlot(grid, GridLength.Auto, useColumns);
        }

        DefineSlot(grid, partLength, useColumns);
    }

    private Border? GridForDockGroup(IDockGroup group)
        => group.IsEmpty ? this.GroupWithNoDocks(group) : this.GroupWithDocks(group);

    private Border GroupWithDocks(IDockGroup group)
    {
        var grid = new Grid
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };
        grid.ColumnDefinitions.Add(new ColumnDefinition() { Width = new GridLength(1, GridUnitType.Star) });
        grid.Children.Add(new TextBlock() { Text = group.ToString() });
        return new Border()
        {
            Child = grid,
            BorderBrush = this.BorderBrush,
            BorderThickness = this.BorderThickness,
        };
    }

    private Border? GroupWithNoDocks(IDockGroup group)
    {
        if (group.First is null && group.Second is null)
        {
            return null;
        }

        var grid = new Grid
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        var partLength = new GridLength(group.First is null || group.Second is null ? 1.0 : 0.5, GridUnitType.Star);
        var useColumns = group.Orientation is Docking.Orientation.Horizontal or Docking.Orientation.Horizontal;

        DefineSlots(group, grid, partLength, useColumns);

        if (group.First != null)
        {
            this.AddContentForSlot(grid, group.First, 0, 0);
        }

        if (group.Second != null)
        {
            if (group.First != null)
            {
                AddSplitter(grid, useColumns ? 0 : 1, useColumns ? 1 : 0);
            }

            this.AddContentForSlot(grid, group.Second, useColumns ? 0 : 2, useColumns ? 2 : 0);
        }

        return new Border()
        {
            Child = grid,
            BorderBrush = this.BorderBrush,
            BorderThickness = this.BorderThickness,
        };
    }

    private void AddContentForSlot(Grid grid, IDockGroup group, int row, int column)
    {
        var content = this.GridForDockGroup(group);
        if (content == null)
        {
            return;
        }

        content.SetValue(Grid.RowProperty, row);
        content.SetValue(Grid.ColumnProperty, column);
        grid.Children.Add(content);
    }
}
