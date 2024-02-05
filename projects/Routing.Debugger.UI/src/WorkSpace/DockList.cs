// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Colors = Microsoft.UI.Colors;

/// <summary>
/// A custom control representing the list of docks in a dock group. It uses a
/// vector grid with a single row or column based on the group's orientation.
/// </summary>
public class DockList : VectorGrid
{
    public DockList(IDockGroup group)
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
                    Child = new Viewbox() { Child = new TextBlock() { Text = dock.ToString() } },
                    BorderBrush = new SolidColorBrush(Colors.Blue),
                    BorderThickness = new Thickness(0.5),
                    Margin = new Thickness(4),
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
