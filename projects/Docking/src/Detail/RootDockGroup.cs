// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics;

internal sealed class RootDockGroup : DockGroup
{
    private readonly DockGroup center = new();
    private DockGroup? left;
    private DockGroup? top;
    private DockGroup? right;
    private DockGroup? bottom;

    public RootDockGroup() => this.AddGroupFirst(this.center, DockGroupOrientation.Undetermined);

    public void DockCenter(IDock dock)
    {
        if (!this.center.IsEmpty)
        {
            throw new InvalidOperationException(
                "the root center group is already populated, dock relative to its content");
        }

        this.center.AddDock(dock);
    }

    public void DockLeft(IDock dock)
    {
        if (this.left == null)
        {
            this.left = NewDockGroupWithTray(dock, AnchorPosition.Left);
            this.AddBeforeCenter(this.left, DockGroupOrientation.Horizontal);
            return;
        }

        AppendToRoot(this.left, NewDockGroup(dock), DockGroupOrientation.Horizontal);
    }

    public void DockTop(IDock dock)
    {
        if (this.top == null)
        {
            this.top = NewDockGroupWithTray(dock, AnchorPosition.Top);
            this.AddBeforeCenter(this.top, DockGroupOrientation.Vertical);
            return;
        }

        AppendToRoot(this.top, NewDockGroup(dock), DockGroupOrientation.Vertical);
    }

    public void DockRight(IDock dock)
    {
        if (this.right == null)
        {
            this.right = NewDockGroupWithTray(dock, AnchorPosition.Right);
            this.AddAfterCenter(this.right, DockGroupOrientation.Horizontal);
            return;
        }

        this.PrependToRoot(this.right, NewDockGroup(dock), DockGroupOrientation.Horizontal);
    }

    public void DockBottom(IDock dock)
    {
        if (this.bottom == null)
        {
            this.bottom = NewDockGroupWithTray(dock, AnchorPosition.Bottom);
            this.AddAfterCenter(this.bottom, DockGroupOrientation.Vertical);
            return;
        }

        this.PrependToRoot(this.bottom, NewDockGroup(dock), DockGroupOrientation.Vertical);
    }

    internal override void RemoveGroup(IDockGroup group)
    {
        // The root groups are protected and cannot be removed.
        if (group == this.center || group == this.left || group == this.right || group == this.bottom ||
            group == this.top || group is IDockTray)
        {
            return;
        }

        base.RemoveGroup(group);
    }

    private static DockGroup NewDockGroup(IDock dock)
    {
        var newGroup = new DockGroup();
        newGroup.AddDock(dock);
        return newGroup;
    }

    private static DockGroup NewDockGroupWithTray(IDock dock, AnchorPosition position)
    {
        var trayHolder = new DockGroup();
        var newGroup = new DockGroup();
        newGroup.AddDock(dock);

        var orientation
            = position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Horizontal
                : DockGroupOrientation.Vertical;

        if (position is AnchorPosition.Left or AnchorPosition.Top)
        {
            trayHolder.AddGroupFirst(new TrayGroup(position), orientation);
            trayHolder.AddGroupLast(newGroup, orientation);
        }
        else
        {
            trayHolder.AddGroupFirst(newGroup, orientation);
            trayHolder.AddGroupLast(new TrayGroup(position), orientation);
        }

        return trayHolder;
    }

    private static void AppendToRoot(DockGroup root, IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = LastAvailableSlot(root).AsDockGroup();
        parent.AddGroupLast(group, orientation);
    }

    private static IDockGroup LastAvailableSlot(IDockGroup root)
    {
        IDockGroup? result = null;
        IDockGroup? furthestNode = null;
        var maxDepth = -1;
        var furthestDepth = -1;

        DepthFirstSearch(root, 0, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);

        result ??= furthestNode;
        Debug.Assert(result is not null, "we always have a node to add to");
        return result;
    }

    private static void DepthFirstSearch(
        IDockGroup? node,
        int depth,
        ref IDockGroup? result,
        ref int maxDepth,
        ref IDockGroup? furthestNode,
        ref int furthestDepth)
    {
        if (node == null)
        {
            return;
        }

        if ((node.First == null || node.Second == null) && depth > maxDepth)
        {
            result = node;
            maxDepth = depth;
        }

        if (depth > furthestDepth)
        {
            furthestNode = node;
            furthestDepth = depth;
        }

        DepthFirstSearch(node.Second, depth + 1, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);
        DepthFirstSearch(node.First, depth + 1, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);
    }

    private void PrependToRoot(DockGroup root, IDockGroup group, DockGroupOrientation orientation)
    {
        // In the root groups, we always want the center to be before any other
        // groups with docks.
        if (root.First == this.center)
        {
            root.AddGroupAfter(group, this.center, orientation);
        }
        else
        {
            root.AddGroupFirst(group, orientation);
        }
    }

    private void AddBeforeCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent as DockGroup;
        Debug.Assert(parent is not null, $"center always has a parent of type {nameof(DockGroup)}");
        parent.AddGroupBefore(group, this.center, orientation);
    }

    private void AddAfterCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent as DockGroup;
        Debug.Assert(parent is not null, $"center always has a parent of type {nameof(DockGroup)}");
        parent.AddGroupAfter(group, this.center, orientation);
    }
}
