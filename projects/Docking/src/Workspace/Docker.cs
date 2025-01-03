// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Utils;
using static DroidNet.Docking.Workspace.TreeTraversal;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// The main interface to the docking workspace. Manages the docks and the workspace layout.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="Docker"/> class is responsible for managing the docking operations and layout of dockable entities within a workspace.
/// It provides methods to dock, undock, minimize, pin, float, and resize docks, ensuring a flexible and organized workspace.
/// </para>
/// <para>
/// This class also handles the layout changes and triggers events to notify when the layout is updated.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of <see cref="Docker"/> and initialize the docking workspace, use the following code:
/// </para>
/// <code><![CDATA[
/// var docker = new Docker();
/// var centerDock = CenterDock.New();
/// var anchor = new Anchor(AnchorPosition.Center);
/// docker.Dock(centerDock, anchor);
/// ]]></code>
/// </example>
public partial class Docker : IDocker
{
    private readonly DockingTreeNode root;
    private readonly CenterGroup center;
    private readonly Dictionary<AnchorPosition, LayoutSegment?> edges = [];

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="Docker"/> class.
    /// </summary>
    public Docker()
    {
        this.root = new DockingTreeNode(this, new LayoutGroup(this));
        this.center = new CenterGroup(this);
        var centerNode = new DockingTreeNode(this, this.center);
        try
        {
            this.root.AddChildLeft(centerNode, DockGroupOrientation.Horizontal);
            centerNode = null; // Dispose ownership transferred to root
        }
        finally
        {
            centerNode?.Dispose();
        }

        this.edges.Add(AnchorPosition.Left, default);
        this.edges.Add(AnchorPosition.Right, default);
        this.edges.Add(AnchorPosition.Top, default);
        this.edges.Add(AnchorPosition.Bottom, default);
    }

    /// <inheritdoc/>
    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

    /// <inheritdoc/>
    public void DumpWorkspace() => this.root.Dump();

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="Docker"/> and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/> to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            /* Dispose of managed resources */
            this.root.Dispose();
            this.center.Dispose(); // For completeness, but should have been disposed of already in the root
        }

        /* Dispose of unmanaged resources */

        this.isDisposed = true;
    }
}

/// <summary>Dock management.</summary>
public partial class Docker
{
    /// <inheritdoc/>
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
                var groupNode = this.FindNode(group) ?? throw new ArgumentException(
                    $"anchor dock is in a group ({group}) that cannot be found in the docking tree",
                    nameof(anchor));
                group = groupNode.Repartition(anchorDock, requiredOrientation);
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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
        try
        {
            if (edge.Left?.Value is TrayGroup)
            {
                edge.AddChildRight(newNode, orientation);
            }
            else
            {
                edge.AddChildLeft(newNode, orientation);
            }

            newNode = null; // Dispose ownership transferred to edge
        }
        finally
        {
            newNode?.Dispose();
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

        var node = this.FindNode(dock.Group)!;

        // Walk the docking tree up until we find a dock group which implements
        // the IDockTray interface. This would be one of the root edge groups.
        while (node is not null)
        {
            if (node.Left?.Value is TrayGroup leftAsTray)
            {
                return leftAsTray;
            }

            if (node.Right?.Value is TrayGroup rightAsTray)
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
                DockToEdge(AnchorPosition.Left, DockGroupOrientation.Horizontal);
                break;

            case AnchorPosition.Top:
                DockToEdge(AnchorPosition.Top, DockGroupOrientation.Vertical);
                break;

            case AnchorPosition.Right:
                DockToEdge(AnchorPosition.Right, DockGroupOrientation.Horizontal);
                break;

            case AnchorPosition.Bottom:
                DockToEdge(AnchorPosition.Bottom, DockGroupOrientation.Vertical);
                break;

            case AnchorPosition.Center:
                this.DockToCenter(dock);
                break;

            case AnchorPosition.With:
                throw new InvalidOperationException("docking to the root cannot be `With`");

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(AnchorPosition));
        }

        void DockToEdge(AnchorPosition anchorPosition, DockGroupOrientation dockGroupOrientation)
        {
            DockingTreeNode? newNode = null;

            DockingTreeNode edgeNode;
            var edgeSegment = this.edges[position];
            if (edgeSegment is not null)
            {
                // We must always find an edge node if there is a non-null edge segment corresponding to it.
                edgeNode = this.FindNode(edgeSegment)!;
            }
            else
            {
                edgeNode = this.InitEdgeNode(anchorPosition);
                newNode = edgeNode;
            }

            var layoutDockGroup = this.NewLayoutDockGroup(dock);
            try
            {
                this.AddToEdge(edgeNode, layoutDockGroup, dockGroupOrientation);
                newNode = null; // Dispose ownership transferred
                layoutDockGroup = null; // Dispose ownership transferred
            }
            finally
            {
                newNode?.Dispose();
                layoutDockGroup?.Dispose();
            }
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

    private DockingTreeNode InitEdgeNode(AnchorPosition position)
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

        var disposeTrayNode = new DockingTreeNode(this, new TrayGroup(this, position));
        var disposeEdgeNode = new DockingTreeNode(this, new EdgeGroup(this, orientation));

        try
        {
            if (position is AnchorPosition.Left or AnchorPosition.Top)
            {
                disposeEdgeNode.AddChildLeft(disposeTrayNode, orientation);
                this.AddBeforeCenter(disposeEdgeNode, orientation);
            }
            else
            {
                disposeEdgeNode.AddChildRight(disposeTrayNode, orientation);
                this.AddAfterCenter(disposeEdgeNode, orientation);
            }

            this.edges[position] = disposeEdgeNode.Value;

            var newNode = disposeEdgeNode;

            disposeTrayNode = null; // Dispose ownership transferred
            disposeEdgeNode = null; // Dispose ownership transferred

            return newNode;
        }
        finally
        {
            disposeEdgeNode?.Dispose();
            disposeTrayNode?.Dispose();
        }
    }

    private DockingTreeNode FindCenterNode()
    {
        var centerNode = this.FindNode(this.center);
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

    private DockingTreeNode? FindNode(LayoutSegment segment)
    {
        DockingTreeNode? result = null;

        DepthFirstInOrder(this.root, Visitor);
        return result;

        bool Visitor(BinaryTreeNode<LayoutSegment>? node)
        {
            if (node is null || node.Value != segment)
            {
                // Continue traversal
                return true;
            }

            result = (DockingTreeNode?)node;

            // Stop traversal
            return false;
        }
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
    /// <inheritdoc/>
    public void Layout(LayoutEngine layoutEngine)
    {
        var flow = layoutEngine.StartLayout(this.root.Value);
        layoutEngine.PushFlow(flow);
        Layout(this.root, layoutEngine);
        Debug.Assert(flow == layoutEngine.CurrentFlow, "some pushes were not matched by pops");

        // $"=== Final state: {layoutEngine.CurrentFlow}"
        layoutEngine.EndLayout();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Roslynator",
        "RCS1134:Remove redundant statement",
        Justification = "Use explicit return statement to separate functions defined locally")]
    private static void Layout(DockingTreeNode node, LayoutEngine layoutEngine)
    {
        if (!ShouldShowNodeRecursive(node))
        {
            // $"Skipping {node.Value}"
            return;
        }

        // $"Layout started for item {node.Value}"
        var reoriented = ReorientFlowIfNeeded(node.Value);
        if (reoriented)
        {
            var flow = layoutEngine.StartFlow(node.Value);
            layoutEngine.PushFlow(flow);
        }

        if (node.Value is DockGroup group && group.Docks.Count != 0)
        {
            // A group with docks -> Layout the docks as items in the vector grid.
            foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
            {
                layoutEngine.PlaceDock(dock);
            }
        }
        else
        {
            HandlePart(node.Left);
            HandlePart(node.Right);
        }

        if (reoriented)
        {
            layoutEngine.EndFlow();
            layoutEngine.PopFlow();
        }

        return;

        static bool ShouldShowNodeRecursive(DockingTreeNode? node)
        {
            return node is not null
&& (node.Value is DockGroup dockGroup
                ? node.Value is TrayGroup tray
                    ? tray.Docks.Count != 0
                    : dockGroup.Docks.Any(d => d.State != DockingState.Minimized)
                : node.Value is LayoutGroup
                ? ShouldShowNodeRecursive(node.Left) || ShouldShowNodeRecursive(node.Right)
                : throw new UnreachableException());
        }

        bool ReorientFlowIfNeeded(ILayoutSegment segment)
        {
            // For the sake of layout, if a group has Docks and only one of them is
            // pinned, we consider the group's orientation as Undetermined. That
            // way, we don't create a new grid for that group.
            var orientation = segment.Orientation;
            if (segment is DockGroup dockGroup &&
                dockGroup.Docks.Where(d => d.State == DockingState.Pinned).Take(2).Count() == 1)
            {
                // "Group has only one pinned dock, considering it as DockGroupOrientation.Undetermined"
                orientation = DockGroupOrientation.Undetermined;
            }

            var flow = layoutEngine.CurrentFlow;
            return orientation != DockGroupOrientation.Undetermined &&
                   flow.Direction != segment.Orientation.ToFlowDirection();
        }

        void HandlePart(DockingTreeNode? child)
        {
            if (child is null)
            {
                return;
            }

            if (child.Value is TrayGroup tray && tray.Docks.Count != 0)
            {
                Debug.Assert(
                    (tray.Orientation == DockGroupOrientation.Vertical && layoutEngine.CurrentFlow.IsHorizontal) ||
                    (tray.Orientation == DockGroupOrientation.Horizontal && layoutEngine.CurrentFlow.IsVertical),
                    $"expecting tray orientation {child.Value.Orientation} to be orthogonal to flow direction {layoutEngine.CurrentFlow.Direction}");

                // Place the tray if it's not empty.
                layoutEngine.PlaceTray(tray);
                return;
            }

            Layout(child, layoutEngine);
        }
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
            node.Value.StretchToFill = true;
            node = node.Parent;
        }
    }
}
