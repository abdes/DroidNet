// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Utils;

public class Docker : IDocker, IOptimizingDocker
{
    private readonly RootDockGroup root;

    private int isConsolidating;
    private bool disposed;

    public Docker() => this.root = new RootDockGroup(this);

    public event Action<LayoutChangeReason>? LayoutChanged;

    public IDockGroup Root => this.root;

    public void Dock(IDock dock, Anchor anchor, bool minimized = false)
    {
        /*
         * TODO: need to handle docking when the anchor is 'With'
         */

        if (dock.State != DockingState.Undocked)
        {
            Undock(dock);
        }

        var anchorDock = anchor.RelativeTo?.Owner?.AsDock() ?? throw new ArgumentException(
            $"invalid anchor for relative docking: {anchor}",
            nameof(anchor));
        var group = anchorDock.Group ?? throw new ArgumentException(
            $"dock `{dock}` does not belong to a group and cannot be used as an anchor",
            nameof(anchor));

        group.AddDock(dock, anchor);
        dock.AsDock().Anchor = anchor;
        dock.AsDock().Docker = this;

        if (minimized)
        {
            this.MinimizeDock(dock);
        }
        else
        {
            this.PinDock(dock);
        }

        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void DockToCenter(IDock dock)
    {
        // Sanity checks on the dock type used for the center dock
        Debug.Assert(!dock.CanMinimize, "the center dock cannot be minimized");
        Debug.Assert(!dock.CanClose, "the center dock cannot be closed");

        if (dock.State != DockingState.Undocked)
        {
            Undock(dock);
        }

        this.root.DockCenter(dock);
        dock.AsDock().Docker = this;

        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void DockToRoot(IDock dock, AnchorPosition position, bool minimized = false)
    {
        if (dock.State != DockingState.Undocked)
        {
            Undock(dock);
        }

        dock.AsDock().Anchor = new Anchor(position);
        dock.AsDock().Docker = this;

        switch (position)
        {
            case AnchorPosition.Left:
                this.root.DockLeft(dock);
                break;

            case AnchorPosition.Top:
                this.root.DockTop(dock);
                break;

            case AnchorPosition.Right:
                this.root.DockRight(dock);
                break;

            case AnchorPosition.Bottom:
                this.root.DockBottom(dock);
                break;

            case AnchorPosition.With:
            case AnchorPosition.Center:
                throw new InvalidOperationException("docking to the root can only be done along the edges");

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(AnchorPosition));
        }

        if (minimized)
        {
            this.MinimizeDock(dock);
        }
        else
        {
            this.PinDock(dock);
        }

        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void MinimizeDock(IDock dock)
    {
        if (dock.State == DockingState.Minimized)
        {
            return;
        }

        if (!dock.CanMinimize)
        {
            throw new InvalidOperationException($"dock `{dock}` cannot be minimized");
        }

        if (dock.State == DockingState.Floating)
        {
            // TODO: implement floating hide
            dock.AsDock().State = DockingState.Minimized;
            this.LayoutChanged?.Invoke(LayoutChangeReason.Floating);
            return;
        }

        // Add the dock to the minimized docks list of the closest tray group.
        var tray = FindTrayForDock(dock.AsDock());
        tray.AddDock(dock);
        dock.AsDock().State = DockingState.Minimized;
        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void PinDock(IDock dock)
    {
        if (dock.State == DockingState.Pinned)
        {
            return;
        }

        if (dock.State is DockingState.Minimized or DockingState.Floating)
        {
            // Remove the dock from the minimized docks list of the closest tray group.
            var tray = FindTrayForDock(dock.AsDock());
            var removed = tray.RemoveDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        dock.AsDock().State = DockingState.Pinned;

        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void ResizeDock(IDock dock, Width? width, Height? height)
    {
        var sizeChanged = false;

        if (width != null && !dock.Width.Equals(width))
        {
            dock.AsDock().Width = width;
            sizeChanged = true;
        }

        if (height != null && !dock.Height.Equals(height))
        {
            dock.AsDock().Height = height;
            sizeChanged = true;
        }

        if (sizeChanged && dock.State == DockingState.Pinned)
        {
            this.LayoutChanged?.Invoke(LayoutChangeReason.Resize);
        }
    }

    public void CloseDock(IDock dock)
    {
        if (!dock.CanClose)
        {
            throw new InvalidOperationException($"dock `{dock}` cannot be closed");
        }

        Undock(dock);

        if (dock is IDisposable resource)
        {
            resource.Dispose();
        }

        this.LayoutChanged?.Invoke(LayoutChangeReason.Docking);
    }

    public void FloatDock(IDock dock)
    {
        if (dock.State == DockingState.Floating)
        {
            return;
        }

        if (dock.State is not DockingState.Minimized)
        {
            throw new InvalidOperationException($"cannot float a dock {dock} / {dock.State} that is not minimized");
        }

        // TODO: implement floating show
        dock.AsDock().State = DockingState.Floating;

        this.LayoutChanged?.Invoke(LayoutChangeReason.Floating);
    }

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.root.Dispose();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    public void ConsolidateUp(IDockGroup startingGroup)
    {
        if (Interlocked.Exchange(ref this.isConsolidating, 1) == 1)
        {
            return;
        }

        try
        {
            var result = ConsolidateIfNeeded(
                startingGroup as DockGroup ?? throw new ArgumentException(
                    $"expecting a object of type {typeof(DockGroup)}",
                    nameof(startingGroup)));
            while (result is not null)
            {
                result = ConsolidateIfNeeded(result);
            }
        }
        finally
        {
            _ = Interlocked.Exchange(ref this.isConsolidating, 0);
        }
    }

    private static TrayGroup FindTrayForDock(Dock dock)
    {
        // Walk the docking tree up until we find a dock group which implements
        // the IDockTray interface. This would be one of the root edge groups.
        var group = dock.Group;
        while (group != null)
        {
            if (group.First is TrayGroup firstAsTray)
            {
                return firstAsTray;
            }

            if (group.Second is TrayGroup secondAsTray)
            {
                return secondAsTray;
            }

            group = group.Parent as DockGroup;
        }

        throw new InvalidOperationException($"dock `{dock}` cannot be minimized. Could not find a tray in its branch.");
    }

    private static DockGroup? ConsolidateIfNeeded(DockGroup group)
    {
        // Center and edge groups cannot be optimized
        if (group.IsCenter || group.IsEdge)
        {
            return null;
        }

        if (group.Parent is not DockGroup parent)
        {
            return null;
        }

        parent.DumpGroup();

        try
        {
            if (group.IsLeaf)
            {
                if (group is { IsEmpty: true })
                {
                    parent.RemoveGroup(group);
                    return parent;
                }

                // Potential for collapsing into parent or merger with sibling
                return group.Sibling is null or DockGroup { IsLeaf: true } ? parent : null;
            }

            // Single child parent
            if (group.First is null || group.Second is null)
            {
                var child = group.First == null ? group.Second!.AsDockGroup() : group.First.AsDockGroup();

                // with compatible orientation => assimilate child into parent
                if (child.Orientation == DockGroupOrientation.Undetermined || child.Orientation == group.Orientation)
                {
                    group.AssimilateChild(child);
                    return group;
                }
            }
            else
            {
                // Parent with two leaf children
                if (group is { First: DockGroup { IsLeaf: true }, Second: DockGroup { IsLeaf: true } })
                {
                    // and compatible orientations => merge the children
                    if ((group.First.Orientation == DockGroupOrientation.Undetermined ||
                         group.First.Orientation == group.Orientation) &&
                        (group.Second.Orientation == DockGroupOrientation.Undetermined ||
                         group.Second.Orientation == group.Orientation))
                    {
                        group.MergeLeafParts();
                        return group;
                    }
                }
            }

            return null;
        }
        finally
        {
            parent.DumpGroup();
        }
    }

    private static void Undock(IDock dock)
    {
        if (dock.State == DockingState.Undocked)
        {
            return;
        }

        if (dock.State is DockingState.Minimized or DockingState.Floating)
        {
            // Remove the dock from the minimized docks list of the closest tray group.
            var tray = FindTrayForDock(dock.AsDock());
            var removed = tray.RemoveDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        // Remove the dock from its containing group
        var dockImpl = dock.AsDock();
        Debug.Assert(dockImpl.Group is not null, $"expecting an already docked dock `{dock}` to have a non-null group");
        dockImpl.Group.RemoveDock(dock);
        dock.AsDock().State = DockingState.Undocked;
        dock.AsDock().Group = null;

        dock.AsDock().Docker = null;

        // We do not trigger a LayoutChanged event because the undocking will always be followed by another operation,
        // which, if needed, will trigger the vent.
    }
}
