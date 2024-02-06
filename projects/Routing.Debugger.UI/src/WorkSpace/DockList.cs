// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;
using DroidNet.Routing.Debugger.UI.Docks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A custom control representing the list of docks in a dock group. It uses a
/// vector grid with a single row or column based on the group's orientation.
/// </summary>
public class DockList : VectorGrid
{
    public DockList(IDocker docker, IDockGroup group)
        : base(
            group.Orientation == DockGroupOrientation.Horizontal
                ? Orientation.Horizontal
                : Orientation.Vertical)
    {
        this.Name = group.ToString();

        var isFirst = true;
        var index = 0;

        foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
        {
            this.DefineItem(new GridLength(1, GridUnitType.Star));
            this.AddItem(
                new Border()
                {
                    Child = dock is ApplicationDock
                        ? new Viewbox() { Child = new TextBlock() { Text = dock.ToString() } }
                        : new DockPanel() { ViewModel = new DockPanelViewModel(dock, docker) },

                    // BorderBrush = new SolidColorBrush(Colors.Blue),
                    // BorderThickness = new Thickness(0.5),
                    // Margin = new Thickness(0),
                },
                index++);
            if (!isFirst)
            {
                this.DefineItem(GridLength.Auto);
                this.AddSplitter(index++);
            }

            isFirst = false;
        }
    }
}
