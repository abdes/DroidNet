// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Workspace;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public sealed class GridFlowLayout(IDockViewFactory dockViewFactory) : LayoutEngine
{
    private readonly Dictionary<DockId, CachedView> cachedViews = [];

    public ResizableVectorGrid CurrentGrid => ((GridFlow)this.CurrentFlow).Grid;

    public override LayoutFlow StartLayout(ILayoutSegment segment)
        => new GridFlow(segment)
        {
            Description = $"Root Grid {segment}",
            Grid = new ResizableVectorGrid(ToGridOrientation(segment.Orientation)) { Name = "Workspace Root" },
        };

    public override void PlaceDock(IDock dock)
    {
        // $"Place dock {dock} with Width={dock.Width} and Height={dock.Height}"
        var gridItemLength = this.GetGridLengthForDock(dock);

        // $"GridLength for dock {dock}: {gridItemLength} (orientation is {this.CurrentGrid.Orientation}"
        this.CurrentGrid.AddResizableItem(this.GetViewForDock(dock), gridItemLength, 32);
    }

    public override void PlaceTray(TrayGroup tray)
    {
        Debug.Assert(tray.Docks.Count != 0, "don't place a tray if it is empty");

        // $"Place Tray: {tray}"
        var trayOrientation = tray.Orientation == DockGroupOrientation.Vertical
            ? Orientation.Vertical
            : Orientation.Horizontal;
        var trayViewModel = new DockTrayViewModel(tray, trayOrientation);
        var trayControl = new DockTray { ViewModel = trayViewModel };
        this.CurrentGrid.AddFixedSizeItem(trayControl, GridLength.Auto, 32);
    }

    public override LayoutFlow StartFlow(ILayoutSegment segment)
    {
        // $"New Grid for: {segment}"
        var newVector = new ResizableVectorGrid(ToGridOrientation(segment.Orientation)) { Name = segment.ToString() };

        var length = segment.StretchToFill
            ? new GridLength(1, GridUnitType.Star)
            : GetGridLengthForSegment(segment, newVector.VectorOrientation);

        this.CurrentGrid.AddResizableItem(newVector, length, 32);

        return new GridFlow(segment)
        {
            Description = $"Grid for {segment}",
            Grid = newVector,
        };
    }

    public override void EndFlow() => this.CurrentGrid.AdjustSizes(); /* $"Close flow {this.CurrentGrid}" */

    public override void EndLayout()
    {
        // "Layout ended"
    }

    private static Orientation ToGridOrientation(DockGroupOrientation orientation)
        => orientation == DockGroupOrientation.Vertical ? Orientation.Vertical : Orientation.Horizontal;

    private static GridLength GetGridLengthForSegment(ILayoutSegment segment, Orientation gridOrientation)
    {
        if (segment is LayoutDockGroup group)
        {
            if (gridOrientation == Orientation.Vertical)
            {
                var firstDock = group.Docks.FirstOrDefault(d => !d.Width.IsNullOrEmpty);
                if (firstDock != null)
                {
                    return firstDock.Width.ToGridLength();
                }
            }
            else
            {
                var firstDock = group.Docks.FirstOrDefault(d => !d.Height.IsNullOrEmpty);
                if (firstDock != null)
                {
                    return firstDock.Height.ToGridLength();
                }
            }
        }

        return GridLength.Auto;
    }

    private UIElement GetViewForDock(IDock dock)
    {
        if (this.cachedViews.TryGetValue(dock.Id, out var cachedView))
        {
            var removed = cachedView.ParentGrid.Children.Remove(cachedView.View);
            Debug.Assert(removed, "failed to remove cached view from owner grid for dock {dock}");

            cachedView.ParentGrid = this.CurrentGrid;
            return cachedView.View;
        }

        var newView = dockViewFactory.CreateViewForDock(dock);
        this.cachedViews.Add(
            dock.Id,
            new CachedView()
            {
                ParentGrid = this.CurrentGrid,
                View = newView,
            });
        return newView;
    }

    private GridLength GetGridLengthForDock(IDock dock)
        => this.CurrentFlow.IsHorizontal
            ? dock.Width.ToGridLength()
            : dock.Height
                .ToGridLength(); /* $"GridLength for dock {dock} using {(this.CurrentFlow.IsHorizontal ? "width" : "height")}" */

    private sealed class GridFlow(ILayoutSegment segment) : LayoutFlow(segment)
    {
        public ILayoutSegment Segment { get; } = segment;

        public required ResizableVectorGrid Grid { get; init; }
    }

    private sealed class CachedView
    {
        public required ResizableVectorGrid ParentGrid { get; set; }

        public required UIElement View { get; init; }
    }
}
