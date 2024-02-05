// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Routing.Generators;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

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
    {
        if (!group.IsEmpty)
        {
            return GroupWithDocks(group);
        }

        var content = GroupWithNoDocks(group);
        return content == null
            ? null
            : new Border()
            {
                Child = content,
                BorderBrush = new SolidColorBrush(Colors.Red),
                BorderThickness = new Thickness(0.5),
            };
    }

    private static DockGroup? GroupWithNoDocks(IDockGroup group)
    {
        var showFirst = ShouldShowGroup(group.First);
        var showSecond = ShouldShowGroup(group.Second);

        return showFirst || showSecond ? new DockGroup(group, showFirst, showSecond) : null;
    }

    private static bool ShouldShowGroup(IDockGroup? group)
    {
        if (group is null)
        {
            return false;
        }

        if (group is IDockTray { IsEmpty: false })
        {
            return true;
        }

        if (!group.IsEmpty)
        {
            return group.Docks.Any(d => d.State != DockingState.Minimized);
        }

        return ShouldShowGroup(group.First) || ShouldShowGroup(group.Second);
    }

    private static DockList? GroupWithDocks(IDockGroup group)
        => group.Docks.Any(d => d.State != DockingState.Minimized) ? new DockList(group) : null;

    private sealed class DockGroup : VectorGrid
    {
        public DockGroup(IDockGroup group, bool showFirst, bool showSecond)
            : base(
                group.Orientation == DockGroupOrientation.Horizontal
                    ? Orientation.Horizontal
                    : Orientation.Vertical)
        {
            this.Name = group.ToString();
            this.BuildGrid(group, showFirst, showSecond);
        }

        private static void AddContentForSlot(VectorGrid grid, IDockGroup slot, int index)
        {
            var content = ContentForDockGroup(slot);
            if (content != null)
            {
                grid.AddItem(content, index);
            }
        }

        private void BuildGrid(IDockGroup group, bool showFirst, bool showSecond)
        {
            Debug.Assert(
                showFirst || showSecond,
                "do not build a grid unless at least one part of the group is to be shown");

            var gridItemIndex = 0;
            var showSplitter = false;

            if (showFirst)
            {
                Debug.Assert(group.First is not null, "if you request to show the first part, it should not be null");

                if (!HandleTray(group.First) && !Collapse(group.First, group.First.First, group.First.Second))
                {
                    this.DefineItem(new GridLength(1, GridUnitType.Star));
                    AddContentForSlot(this, group.First!, gridItemIndex++);
                    showSplitter = true;
                }
            }

            if (showSecond)
            {
                Debug.Assert(group.Second is not null, "if you request to show the second part, it should not be null");

                if (!HandleTray(group.Second) && !Collapse(group.Second, group.Second.Second, group.Second.First))
                {
                    if (showSplitter)
                    {
                        this.DefineItem(GridLength.Auto);
                        this.AddSplitter(gridItemIndex++);
                    }

                    this.DefineItem(new GridLength(1, GridUnitType.Star));
                    AddContentForSlot(this, group.Second!, gridItemIndex);
                }
            }

            return;

            bool HandleTray(IDockGroup part)
            {
                if (part is not IDockTray tray)
                {
                    return false;
                }

                this.DefineItem(GridLength.Auto);
                var trayOrientation = part.Orientation == DockGroupOrientation.Horizontal
                    ? Orientation.Horizontal
                    : Orientation.Vertical;
                var trayControl = new DockTray(tray.MinimizedDocks, trayOrientation);
                this.AddItem(trayControl, gridItemIndex++);
                return true;
            }

            bool Collapse(IDockGroup part, IDockGroup? tray, IDockGroup? other)
            {
                // Collapse the group if it has a tray as part and the other
                // part is not to be shown.
                if (tray is not IDockTray || ShouldShowGroup(other))
                {
                    return false;
                }

                this.DefineItem(GridLength.Auto);
                AddContentForSlot(this, part, gridItemIndex++);
                return true;
            }
        }
    }
}
