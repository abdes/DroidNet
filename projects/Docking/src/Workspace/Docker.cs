// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Utils;

/// <summary>The main interface to the docking workspace. Manages the docks and the workspace layout.</summary>
public partial class Docker : IDocker
{
    private readonly DockingTreeNode root;
    private readonly CenterGroup center;
    private readonly Dictionary<AnchorPosition, LayoutSegment?> edges = [];

    private bool disposed;

    public Docker()
    {
        this.root = new DockingTreeNode(this, new LayoutGroup(this));
        this.center = new CenterGroup(this);
        this.root.AddChildLeft(new DockingTreeNode(this, this.center));

        this.edges.Add(AnchorPosition.Left, default);
        this.edges.Add(AnchorPosition.Right, default);
        this.edges.Add(AnchorPosition.Top, default);
        this.edges.Add(AnchorPosition.Bottom, default);
    }

    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

    public void DumpWorkspace() => this.root.Dump();

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
}

/// <summary>Dock management.</summary>
public partial class Docker
{
    public void Dock(IDock dock, Anchor anchor, bool minimized = false)
    {
        if (dock.State != DockingState.Undocked)
        {
            this.Undock(dock);
        }

        if (anchor.RelativeTo is null)
        {
            this.DockToRoot(dock, anchor.Position);
        }
        else
        {
            var anchorDock = anchor.RelativeTo.Owner?.AsDock() ?? throw new ArgumentException(
                $"invalid anchor for relative docking: {anchor}",
                nameof(anchor));

            if (anchor.Position == AnchorPosition.With)
            {
                this.DockWith(dock, anchorDock);
                return;
            }

            if (anchorDock.Group is not LayoutDockGroup group)
            {
                throw new InvalidOperationException(
                    $"dock `{dock}` does not belong to a {nameof(LayoutDockGroup)} and cannot be used as an anchor");
            }

            // Determine the required orientation based on the requested relative positioning.
            var requiredOrientation = anchor.Position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Horizontal
                : DockGroupOrientation.Vertical;

            // If the required orientation is different from our orientation, we need to repartition by splitting it around
            // the relativeTo dock. Otherwise, we just add the new dock to the group's dock list.
            if (group.Orientation != DockGroupOrientation.Undetermined && group.Orientation != requiredOrientation)
            {
                var groupNode = this.FindNode((node) => node.Item == group) ?? throw new ArgumentException(
                    $"anchor dock is in a group ({group}) that cannot be found in the docking tree",
                    nameof(anchor));
                group = groupNode.Repartition(group, anchorDock, requiredOrientation);
            }

            group.AddDock(dock, anchor);
        }

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

        this.ConsolidateUp(dock.AsDock().Group);
        this.InitiateLayoutRefresh(LayoutChangeReason.Docking);
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
            this.FireLayoutChangedEvent(LayoutChangeReason.Floating);
            return;
        }

        // Add the dock to the minimized docks list of the closest tray group.
        var tray = this.FindTrayForDock(dock.AsDock());
        tray.AddDock(dock);
        dock.AsDock().State = DockingState.Minimized;

        this.FireLayoutChangedEvent(LayoutChangeReason.Docking);
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
            var tray = this.FindTrayForDock(dock.AsDock());
            var removed = tray.RemoveDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        dock.AsDock().State = DockingState.Pinned;

        this.FireLayoutChangedEvent(LayoutChangeReason.Docking);
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
            this.FireLayoutChangedEvent(LayoutChangeReason.Resize);
        }
    }

    public void CloseDock(IDock dock)
    {
        if (!dock.CanClose)
        {
            throw new InvalidOperationException($"dock `{dock}` cannot be closed");
        }

        this.Undock(dock);
        dock.Dispose();

        this.InitiateLayoutRefresh(LayoutChangeReason.Docking);
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

        this.FireLayoutChangedEvent(LayoutChangeReason.Floating);
    }

    private void DockWith(IDock dock, Dock anchorDock)
    {
        if (!dock.CanClose)
        {
            throw new InvalidOperationException(
                $"dock `{dock}` cannot be closed and therefore cannot be anchored `with`");
        }

        dock.MigrateDockables(anchorDock);
        dock.Dispose();
        this.FireLayoutChangedEvent(LayoutChangeReason.Docking);
    }

    /// <summary>
    /// Adds the specified <paramref name="group" />, as the last group, to the docking tree branch starting at the specified
    /// <paramref name="edge" /> group.
    /// </summary>
    /// <param name="edge">
    /// The edge group (left, top), which determines the docking tree branch to which the <paramref name="group" /> will be
    /// appended.
    /// </param>
    /// <param name="group">The group to be appended at the specified <paramref name="edge" />.</param>
    /// <param name="orientation">The desired orientation of the <see cref="LayoutGroup">layout group</see> that will have
    /// <paramref name="group" /> as a child.</param>
    private void AddToEdge(DockingTreeNode edge, LayoutDockGroup group, DockGroupOrientation orientation)
    {
        var newNode = new DockingTreeNode(this, group);
        if (edge.Left?.Item is TrayGroup)
        {
            edge.AddChildRight(newNode);
        }
        else
        {
            edge.AddChildLeft(newNode);
        }

        // Consistently set the orientation for the nodes created to accomodate the newly added node.
        var node = newNode.Parent;
        while (node is not null && node != edge)
        {
            node.Item.Orientation = orientation;
            node = node.Parent;
        }
    }

    private void Undock(IDock dock)
    {
        if (dock.State == DockingState.Undocked)
        {
            return;
        }

        if (dock.State is DockingState.Minimized or DockingState.Floating)
        {
            // Remove the dock from the minimized docks list of the closest tray group.
            var tray = this.FindTrayForDock(dock.AsDock());
            var removed = tray.RemoveDock(dock);
            Debug.Assert(removed, $"was expecting the dock `{dock}` to be in the tray");
        }

        // Remove the dock from its containing group
        var dockImpl = dock.AsDock();
        Debug.Assert(dockImpl.Group is not null, $"expecting an already docked dock `{dock}` to have a non-null group");
        var group = dockImpl.Group;
        _ = group.RemoveDock(dock);

        dockImpl.State = DockingState.Undocked;
        dockImpl.Docker = null;

        this.ConsolidateUp(group);

        // We do not trigger a LayoutChanged event because the undocking will always be followed by another operation,
        // which, if needed, will trigger the event.
    }

    private TrayGroup FindTrayForDock(Dock dock)
    {
        Debug.Assert(dock.Group is not null, "cannot minimize a dock that does not belong to a group.");

        var node = this.FindNode((node) => node.Item == dock.Group)!;

        // Walk the docking tree up until we find a dock group which implements
        // the IDockTray interface. This would be one of the root edge groups.
        while (node is not null)
        {
            if (node.Left?.Item is TrayGroup leftAsTray)
            {
                return leftAsTray;
            }

            if (node.Right?.Item is TrayGroup rightAsTray)
            {
                return rightAsTray;
            }

            node = node.Parent;
        }

        throw new InvalidOperationException($"dock `{dock}` cannot be minimized. Could not find a tray in its branch.");
    }

    private void DockToRoot(IDock dock, AnchorPosition position)
    {
        switch (position)
        {
            case AnchorPosition.Left:
                this.AddToEdge(
                    this.GetOrInitEdgeNode(AnchorPosition.Left),
                    this.NewLayoutDockGroup(dock),
                    DockGroupOrientation.Horizontal);
                break;

            case AnchorPosition.Top:
                this.AddToEdge(
                    this.GetOrInitEdgeNode(AnchorPosition.Top),
                    this.NewLayoutDockGroup(dock),
                    DockGroupOrientation.Vertical);
                break;

            case AnchorPosition.Right:
                this.AddToEdge(
                    this.GetOrInitEdgeNode(AnchorPosition.Right),
                    this.NewLayoutDockGroup(dock),
                    DockGroupOrientation.Horizontal);
                break;

            case AnchorPosition.Bottom:
                this.AddToEdge(
                    this.GetOrInitEdgeNode(AnchorPosition.Bottom),
                    this.NewLayoutDockGroup(dock),
                    DockGroupOrientation.Vertical);
                break;

            case AnchorPosition.Center:
                this.DockToCenter(dock);
                break;

            case AnchorPosition.With:
                throw new InvalidOperationException("docking to the root cannot be `With`");

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(AnchorPosition));
        }
    }

    private void DockToCenter(IDock dock)
    {
        if (this.center.Docks.Count != 0)
        {
            throw new InvalidOperationException(
                "the root center group is already populated, dock relative to its content");
        }

        // Sanity checks on the dock type used for the center dock
        Debug.Assert(!dock.CanMinimize, "the center dock cannot be minimized");
        Debug.Assert(!dock.CanClose, "the center dock cannot be closed");

        if (dock.State != DockingState.Undocked)
        {
            this.Undock(dock);
        }

        this.center.AddDock(dock);

        dock.AsDock().Docker = this;

        this.InitiateLayoutRefresh(LayoutChangeReason.Docking);
    }

    private DockingTreeNode GetOrInitEdgeNode(AnchorPosition position)
    {
        DockingTreeNode edgeNode;

        var edgeSegment = this.edges[position];
        if (this.edges[position] is null)
        {
            edgeNode = this.MakeEdgeNode(position);
            this.edges[position] = edgeNode.Item;
            edgeSegment = edgeNode.Item;
        }
        else
        {
            // We must always find an edge node if there is a non null edge segment corresponding to it.
            edgeNode = this.FindNode((node) => node.Item == edgeSegment)!;
        }

        return edgeNode;
    }

    private DockingTreeNode MakeEdgeNode(AnchorPosition position)
    {
        if (position is AnchorPosition.Center or AnchorPosition.With)
        {
            throw new ArgumentException(
                "edge nodes can only be positioned left, top, right or bottom",
                nameof(position));
        }

        var orientation = position is AnchorPosition.Left or AnchorPosition.Right
            ? DockGroupOrientation.Horizontal
            : DockGroupOrientation.Vertical;

        var edgeNode = new DockingTreeNode(this, new EdgeGroup(this, orientation));
        var trayNode = new DockingTreeNode(this, new TrayGroup(this, position));

        if (position is AnchorPosition.Left || position is AnchorPosition.Top)
        {
            edgeNode.AddChildLeft(trayNode);
            this.AddBeforeCenter(edgeNode, orientation);
        }
        else
        {
            edgeNode.AddChildRight(trayNode);
            this.AddAfterCenter(edgeNode, orientation);
        }

        return edgeNode;
    }

    private DockingTreeNode FindCenterNode()
    {
        var centerNode = this.FindNode((node) => node.Item == this.center);
        Debug.Assert(centerNode is not null, "center node should be initialized before it is accessed");
        return centerNode;
    }

    /// <summary>Creates a new DockGroup and adds the specified dock to it.</summary>
    /// <param name="dock">The dock to be added to the new DockGroup.</param>
    /// <returns>A new DockGroup containing the specified dock.</returns>
    private LayoutDockGroup NewLayoutDockGroup(IDock dock)
    {
        var newGroup = new LayoutDockGroup(this);
        newGroup.AddDock(dock);
        return newGroup;
    }

    private DockingTreeNode? FindNode(Func<DockingTreeNode, bool> predicate)
    {
        /*
         * We're using an in-order traversal of the tree without recursion by tracking the recently traversed nodes to
         * identify the next one.
         */
        var currentNode = this.root;
        DockingTreeNode? previousNode = null;
        while (currentNode != null)
        {
            DockingTreeNode? nextNode;
            if (currentNode.Right != null && previousNode == currentNode.Right)
            {
                nextNode = currentNode.Parent;
            }
            else if (currentNode.Left == null || previousNode == currentNode.Left)
            {
                // Visit the current node
                Debug.WriteLine($"Visiting {currentNode}");
                if (predicate(currentNode))
                {
                    return currentNode;
                }

                nextNode = currentNode.Right ?? currentNode.Parent;
            }
            else
            {
                nextNode = currentNode.Left;
            }

            previousNode = currentNode;
            currentNode = nextNode;
        }

        return null;
    }

    private void AddBeforeCenter(DockingTreeNode node, DockGroupOrientation orientation)
    {
        var centerNode = this.FindCenterNode();
        Debug.Assert(centerNode.Parent is not null, "center always has a parent");
        centerNode.Parent.AddChildBefore(node, centerNode, orientation);
    }

    private void AddAfterCenter(DockingTreeNode node, DockGroupOrientation orientation)
    {
        var centerNode = this.FindCenterNode();
        Debug.Assert(centerNode.Parent is not null, "center always has a parent");
        centerNode.Parent.AddChildAfter(node, centerNode, orientation);
    }
}

/// <summary>Workspace layout management.</summary>
public partial class Docker
{
    public void Layout(LayoutEngine layoutEngine)
    {
        var flow = layoutEngine.StartLayout(this.root.Item);
        layoutEngine.PushFlow(flow);
        Layout(this.root, layoutEngine);
        Debug.WriteLine($"=== Final state: {layoutEngine.CurrentFlow}");
        Debug.Assert(flow == layoutEngine.CurrentFlow, "some pushes were not matched by pops");
        layoutEngine.EndLayout();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "MA0051:Method is too long",
        Justification = "method contains local functions")]
    private static void Layout(DockingTreeNode node, LayoutEngine layoutEngine)
    {
        static bool ShouldShowNodeRecursive(DockingTreeNode? node)
        {
            if (node is null)
            {
                return false;
            }

            if (node.Item is DockGroup dockGroup)
            {
                return node.Item is TrayGroup tray
                    ? tray.Docks.Count != 0
                    : dockGroup.Docks.Any(d => d.State != DockingState.Minimized);
            }

            if (node.Item is LayoutGroup layoutGroup)
            {
                return ShouldShowNodeRecursive(node.Left) || ShouldShowNodeRecursive(node.Right);
            }

            throw new UnreachableException();
        }

        bool ReorientFlowIfNeeded(LayoutSegment segment, LayoutEngine layoutEngine)
        {
            // For the sake of layout, if a group has Docks and only one of them is
            // pinned, we consider the group's orientation as Undetermined. That
            // way, we don't create a new grid for that group.
            var orientation = segment.Orientation;
            if (segment is DockGroup group &&
                group.Docks.Where(d => d.State == DockingState.Pinned).Take(2).Count() == 1)
            {
                Debug.WriteLine("Group has only one pinned dock, considering it as DockGroupOrientation.Undetermined");
                orientation = DockGroupOrientation.Undetermined;
            }

            var flow = layoutEngine.CurrentFlow;
            return orientation != DockGroupOrientation.Undetermined &&
                   flow.Direction != segment.Orientation.ToFlowDirection();
        }

        void HandlePart(DockingTreeNode? child, LayoutEngine layoutEngine)
        {
            if (child is null)
            {
                return;
            }

            if (child.Item is TrayGroup tray && tray.Docks.Count != 0)
            {
                Debug.Assert(
                    (tray.Orientation == DockGroupOrientation.Vertical && layoutEngine.CurrentFlow.IsHorizontal) ||
                    (tray.Orientation == DockGroupOrientation.Horizontal && layoutEngine.CurrentFlow.IsVertical),
                    $"expecting tray orientation {child.Item.Orientation} to be orthogonal to flow direction {layoutEngine.CurrentFlow.Direction}");

                // Place the tray if it's not empty.
                layoutEngine.PlaceTray(tray);
                return;
            }

            Layout(child, layoutEngine);
        }

        if (!ShouldShowNodeRecursive(node))
        {
            Debug.WriteLine($"Skipping {node.Item}");
            return;
        }

        Debug.WriteLine($"Layout started for item {node.Item}");

        var reoriented = ReorientFlowIfNeeded(node.Item, layoutEngine);
        if (reoriented)
        {
            var flow = layoutEngine.StartFlow(node.Item);
            layoutEngine.PushFlow(flow);
        }

        if (node.Item is DockGroup group && group.Docks.Count != 0)
        {
            // A group with docks -> Layout the docks as items in the vector grid.
            foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
            {
                layoutEngine.PlaceDock(dock);
            }
        }
        else
        {
            HandlePart(node.Left, layoutEngine);
            HandlePart(node.Right, layoutEngine);
        }

        if (reoriented)
        {
            layoutEngine.EndFlow();
            layoutEngine.PopFlow();
        }

        Debug.WriteLine($"Layout ended for Group {node}");
    }

    private void FireLayoutChangedEvent(LayoutChangeReason reason)
        => this.LayoutChanged?.Invoke(this, new LayoutChangedEventArgs(reason));

    private void InitiateLayoutRefresh(LayoutChangeReason reason)
    {
        if (reason is LayoutChangeReason.Docking)
        {
            this.UpdateStretchToFill();
        }

        this.FireLayoutChangedEvent(reason);
    }

    private void UpdateStretchToFill()
    {
        var node = this.FindCenterNode().Parent;
        while (node is not null)
        {
            node.Item.StretchToFill = true;
            node = node.Parent;
        }
    }
}
