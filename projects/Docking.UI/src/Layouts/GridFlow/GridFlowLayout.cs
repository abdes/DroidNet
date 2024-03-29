// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public sealed class GridFlowLayout(IDockViewFactory dockViewFactory) : LayoutEngine
{
    private VectorGrid CurrentGrid => ((GridFlow)this.CurrentFlow).Grid;

    public override VectorGrid Build(IDockGroup root) => ((GridFlow)base.Build(root)).Grid;

    protected override Flow StartLayout(IDockGroup root)
        => new GridFlow(root)
        {
            Description = $"Root Grid {root}",
            Grid = new VectorGrid(ToGridOrientation(root.Orientation)) { Name = "Workspace Root" },
        };

    protected override void PlaceDock(IDock dock)
    {
        Debug.WriteLine($"Place dock {dock} with Width={dock.Width} and Height={dock.Height}");

        var gridItemLength = this.GetGridLengthForDock(dock);
        Debug.WriteLine($"GridLength for dock {dock}: {gridItemLength} (orientation is {this.CurrentGrid.Orientation}");

        this.CurrentGrid.AddResizableItem(dockViewFactory.CreateViewForDock(dock), gridItemLength, 32);
    }

    protected override void PlaceTray(IDockTray tray)
    {
        Debug.Assert(!tray.IsEmpty, "don't place a tray if it is empty");

        Debug.WriteLine($"Place Tray: {tray}");
        var trayOrientation = tray.IsVertical ? Orientation.Vertical : Orientation.Horizontal;
        var trayViewModel = new DockTrayViewModel(tray, trayOrientation);
        var trayControl = new DockTray { ViewModel = trayViewModel };
        this.CurrentGrid.AddFixedSizeItem(trayControl, GridLength.Auto, 32);
    }

    protected override Flow StartFlow(IDockGroup group)
    {
        Debug.WriteLine($"New Grid for: {group}");

        var newGrid = new VectorGrid(ToGridOrientation(group.Orientation)) { Name = group.ToString() };

        var stretch = group.ShouldStretch();
        var length = stretch
            ? new GridLength(1, GridUnitType.Star)
            : GetGridLengthForDockGroup(group, newGrid.Orientation);

        this.CurrentGrid.AddResizableItem(newGrid, length, 32);

        return new GridFlow(group)
        {
            Description = $"Grid for {group}",
            Grid = newGrid,
        };
    }

    protected override void EndFlow() => Debug.WriteLine($"Close flow {this.CurrentGrid}");

    protected override void EndLayout() => Debug.WriteLine("Layout ended");

    private static Orientation ToGridOrientation(DockGroupOrientation orientation)
        => orientation == DockGroupOrientation.Vertical ? Orientation.Vertical : Orientation.Horizontal;

    private static GridLength GetGridLengthForDockGroup(IDockGroup group, Orientation gridOrientation)
    {
        if (gridOrientation == Orientation.Vertical)
        {
            var firstDock = group.Docks.FirstOrDefault(d => !d.Width.IsNullOrEmpty);
            if (firstDock != null)
            {
                return firstDock.GridWidth();
            }
        }
        else
        {
            var firstDock = group.Docks.FirstOrDefault(d => !d.Height.IsNullOrEmpty);
            if (firstDock != null)
            {
                return firstDock.GridHeight();
            }
        }

        return GridLength.Auto;
    }

    private GridLength GetGridLengthForDock(IDock dock)
    {
        Debug.WriteLine($"GridLength for dock {dock} using {(this.CurrentFlow.IsHorizontal ? "width" : "height")}");
        return this.CurrentFlow.IsHorizontal ? dock.GridWidth() : dock.GridHeight();
    }

    private sealed class GridFlow(IDockGroup group) : Flow(group)
    {
        public required VectorGrid Grid { get; init; }
    }
}
