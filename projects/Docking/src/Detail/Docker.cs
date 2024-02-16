// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.ComponentModel;
using System.Diagnostics;

public class Docker : IDocker
{
    private readonly RootDockGroup root = new();

    public event Action? LayoutChanged;

    public IDockGroup Root => this.root;

    public void Dock(IDock dock, Anchor anchor, bool minimized = false)
    {
        Debug.Assert(dock.State == DockingState.Undocked, $"dock is in the wrong state `{dock.State}` to be docked");

        var relativeDockId = anchor.DockId ?? throw new ArgumentException(
            $"use {nameof(this.DockToRoot)} or {nameof(this.DockToCenter)} if you are not docking relative to an existing dock",
            nameof(anchor));

        var relativeTo = Detail.Dock.FromId(relativeDockId) ??
                         throw new ArgumentException($"invalid dock id: {anchor.DockId}", nameof(anchor));

        var group = relativeTo.Group ?? throw new ArgumentException(
            $"dock `{dock}` does not belong to a group and cannot be used as an anchor",
            nameof(anchor));

        group.AddDock(dock, anchor);
        dock.AsDock().Anchor = anchor;

        if (minimized)
        {
            this.MinimizeDock(dock);
        }
        else
        {
            this.PinDock(dock);
        }

        this.LayoutChanged?.Invoke();
    }

    // TODO(abdes): center dock is always pinned and cannot have any other state
    public void DockToCenter(IDock dock)
    {
        this.root.DockCenter(dock);
        this.LayoutChanged?.Invoke();
    }

    public void DockToRoot(IDock dock, AnchorPosition position, bool minimized = false)
    {
        Debug.Assert(dock.State == DockingState.Undocked, $"dock is in the wrong state `{dock.State}` to be docked");

        dock.AsDock().Anchor = new Anchor(position);
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

        this.LayoutChanged?.Invoke();
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
            return;
        }

        // Add the dock to the minimized docks list of the closest tray group.
        var tray = FindTryForDock(dock.AsDock());
        tray.AddMinimizedDock(dock);

        dock.AsDock().State = DockingState.Minimized;

        this.LayoutChanged?.Invoke();
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
            var tray = FindTryForDock(dock.AsDock());
            var removed = tray.RemoveMinimizedDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        dock.AsDock().State = DockingState.Pinned;

        this.LayoutChanged?.Invoke();
    }

    public void CloseDock(IDock dock)
    {
        if (dock.State == DockingState.Undocked)
        {
            return;
        }

        if (!dock.CanClose)
        {
            throw new InvalidOperationException($"dock `{dock}` cannot be closed");
        }

        if (dock.State is DockingState.Minimized or DockingState.Floating)
        {
            // Remove the dock from the minimized docks list of the closest tray group.
            var tray = FindTryForDock(dock.AsDock());
            var removed = tray.RemoveMinimizedDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        // Remove the dock from its containing group
        var dockImpl = dock.AsDock();
        Debug.Assert(dockImpl.Group is not null, $"expecting an already docked dock `{dock}` to have a non-null group");
        dockImpl.Group.RemoveDock(dock);
        dock.AsDock().State = DockingState.Undocked;
        if (dock is IDisposable resource)
        {
            resource.Dispose();
        }

        this.LayoutChanged?.Invoke();
    }

    public void FloatDock(IDock dock)
    {
        if (dock.State != DockingState.Minimized)
        {
            return;
        }

        // TODO: implement floating show
        dock.AsDock().State = DockingState.Floating;

        this.LayoutChanged?.Invoke();
    }

    public void Dispose()
    {
        this.root.Dispose();
        GC.SuppressFinalize(this);
    }

    private static TrayGroup FindTryForDock(Dock dock)
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

        throw new InvalidOperationException(
            $"dock `{dock}` cannot be minimized. Most likely it is part of the root center dock, and in such case, you should not try to minimize it.");
    }
}
