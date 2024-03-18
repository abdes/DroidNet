// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Controls.DockTray;
using DroidNet.Docking.Layouts;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public sealed class GridFlow(IDockViewFactory dockViewFactory)
    : LayoutEngine()
{
    private new GridLayoutState CurrentState => (GridLayoutState)base.CurrentState;

    private VectorGrid CurrentGrid => this.CurrentState.CurrentFlow;

    public override VectorGrid Build(IDockGroup root) => ((GridLayoutState)base.Build(root)).CurrentFlow;

    protected override LayoutState StartLayout(IDockGroup root)
        => new GridLayoutState
        {
            Description = $"Root Grid {root}",
            CurrentFlow = new VectorGrid(ToGridOrientation(root.Orientation)) { Name = "Workspace Root" },
        };

    protected override void PlaceDock(IDock dock)
    {
        Debug.WriteLine($"Place dock {dock} with Width={dock.Width} and Height={dock.Height}");

        var gridItemLength = GetGridLengthForDock(dock, this.CurrentGrid.Orientation);
        Debug.WriteLine($"GridLength for dock {dock}: {gridItemLength} (orientation is {this.CurrentGrid.Orientation}");

        this.CurrentState.CurrentFlow.AddResizableItem(
            dockViewFactory.CreateViewForDock(dock),
            gridItemLength,
            32);
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

    protected override LayoutState StartFlow(IDockGroup group)
    {
        Debug.WriteLine($"New Grid for: {group}");

        var newFlow = new VectorGrid(ToGridOrientation(group.Orientation)) { Name = group.ToString() };

        var stretch = group.ShouldStretch();
        var length = stretch
            ? new GridLength(1, GridUnitType.Star)
            : GetGridLengthForDockGroup(group, newFlow.Orientation);

        this.CurrentGrid.AddResizableItem(newFlow, length, 32);

        return new GridLayoutState()
        {
            Description = $"Grid for {group}",
            CurrentFlow = newFlow,
        };
    }

    protected override void EndFlow() => Debug.WriteLine($"Close flow {this.CurrentGrid}");

    protected override void EndLayout() => Debug.WriteLine($"Layout ended");

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

    private static GridLength GetGridLengthForDock(IDock dock, Orientation gridOrientation)
    {
        Debug.WriteLine(
            $"GridLength for dock {dock} using {(gridOrientation == Orientation.Horizontal ? "width" : "height")}");
        return gridOrientation == Orientation.Horizontal ? dock.GridWidth() : dock.GridHeight();
    }

    private sealed class GridLayoutState : LayoutState
    {
        public required VectorGrid CurrentFlow { get; init; }

        public override Docking.FlowDirection FlowDirection
            => this.CurrentFlow.Orientation == Orientation.Horizontal
                ? Docking.FlowDirection.LeftToRight
                : Docking.FlowDirection.TopToBottom;
    }
}
