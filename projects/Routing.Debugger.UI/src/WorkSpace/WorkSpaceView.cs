// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;
using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Colors = Microsoft.UI.Colors;

/// <summary>
/// A custom control to represent the entire docking tree. Each docking group is
/// represented as a two-item grid, in a single row or column, based on the
/// group's orientation.
/// </summary>
[ViewModel(typeof(WorkSpaceViewModel))]
public partial class WorkSpaceView : UserControl
{
    public WorkSpaceView()
    {
        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        this.ViewModelChanged += (_, _) => this.Content = ContentForDockGroup(this.ViewModel.Root);
    }

    private static UIElement? ContentForDockGroup(IDockGroup group)
        => group.IsEmpty ? GroupWithNoDocks(group) : GroupWithDocks(group);

    private static DockGroup? GroupWithNoDocks(IDockGroup group)
    {
        if (group.First is null && group.Second is null)
        {
            return null;
        }

        return new DockGroup(group);
    }

    private static DockList GroupWithDocks(IDockGroup group) => new(group);

    private sealed class DockGroup : ContentControl
    {
        public DockGroup(IDockGroup group)
        {
            this.HorizontalContentAlignment = HorizontalAlignment.Stretch;
            this.VerticalContentAlignment = VerticalAlignment.Stretch;

            this.Grid = new VectorGrid(
                group.Orientation == DockGroupOrientation.Horizontal
                    ? Orientation.Horizontal
                    : Orientation.Vertical);
            this.BuildGrid(group);

            this.Content = new Border()
            {
                Child = this.Grid,
                BorderBrush = new SolidColorBrush(Colors.Red),
                BorderThickness = new Thickness(0.5),
            };
        }

        private VectorGrid Grid { get; set; }

        private void BuildGrid(IDockGroup group)
        {
            var partLength = new GridLength(group.First is null || group.Second is null ? 1.0 : 0.5, GridUnitType.Star);
            if (group.First != null)
            {
                this.Grid.DefineItem(partLength);
                this.AddContentForSlot(group.First, 0);
            }

            if (group.Second == null)
            {
                return;
            }

            if (group.First != null)
            {
                this.Grid.DefineItem(GridLength.Auto);
                this.Grid.AddSplitter(1);
            }

            this.Grid.DefineItem(partLength);
            this.AddContentForSlot(group.Second, 2);
        }

        private void AddContentForSlot(IDockGroup slot, int index)
        {
            var content = ContentForDockGroup(slot);
            if (content != null)
            {
                this.Grid.AddItem(content, index);
            }
        }
    }
}
