// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using CommunityToolkit.WinUI.Controls;
using DroidNet.Docking;
using DroidNet.Routing.Debugger.UI.Styles;
using DroidNet.Routing.Generators;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

[ViewModel(typeof(WorkSpaceViewModel))]
public partial class WorkSpaceView : ContentControl
{
    public WorkSpaceView()
    {
        this.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        this.VerticalContentAlignment = VerticalAlignment.Stretch;
        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        this.ViewModelChanged += (_, _) => this.UpdateContent();
    }

    private void UpdateContent() => this.Content = this.GridForDockGroup(this.ViewModel.Root);

    private UIElement? GridForDockGroup(IDockGroup group)
        => group.IsEmpty ? this.GroupWithNoDocks(group) : this.GroupWithDocks(group);

    private UIElement GroupWithDocks(IDockGroup group)
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

    private UIElement? GroupWithNoDocks(IDockGroup group)
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

        if (group.First != null)
        {
            DefineSlot(grid, partLength, useColumns);
            this.AddContentForSlot(grid, group.First, 0, 0);
        }

        if (group.Second != null)
        {
            if (group.First != null)
            {
                DefineSlot(grid, GridLength.Auto, useColumns);
                AddSeparator(grid, useColumns ? 0 : 1, useColumns ? 1 : 0);
            }

            DefineSlot(grid, partLength, useColumns);
            this.AddContentForSlot(grid, group.Second, useColumns ? 0 : 2, useColumns ? 2 : 0);
        }

        return new Border()
        {
            Child = grid,
            BorderBrush = this.BorderBrush,
            BorderThickness = this.BorderThickness,
        };
    }

    private static void AddSeparator(Grid grid, int row, int column)
    {
        var splitter = row == 0
            ? new DockingSplitter()
            {
                Width = 6,
                VerticalAlignment = VerticalAlignment.Stretch,
            }
            : new GridSplitter()
            {
                Height = 6,
                HorizontalAlignment = HorizontalAlignment.Stretch,
            };

        splitter.Background = new SolidColorBrush(Colors.Transparent);
        splitter.Padding = new Thickness(0);
        splitter.ResizeBehavior = GridSplitter.GridResizeBehavior.PreviousAndNext;
        splitter.ResizeDirection = GridSplitter.GridResizeDirection.Auto;
        splitter.SetValue(Grid.RowProperty, row);
        splitter.SetValue(Grid.ColumnProperty, column);

        grid.Children.Add(splitter);
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
}
