// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics;

public class RootDockGroup : DockGroup
{
    // The center dock group is protected and cannot be removed
    private readonly DockGroup center = new() { IsProtected = true };
    private DockGroup? left;
    private DockGroup? top;
    private DockGroup? right;
    private DockGroup? bottom;

    public RootDockGroup() => this.AddGroupFirst(this.center, DockGroupOrientation.Undetermined);

    public IDockGroup Center => this.center;

    public void DockCenter(IDock dock)
    {
        if (!this.center.IsEmpty)
        {
            throw new InvalidOperationException(
                $"the root center group is already populated, dock relative to its content");
        }

        this.center.AddDock(dock);
    }

    public void DockLeft(DockGroup group)
    {
        if (this.left == null)
        {
            this.AddBeforeCenter(group, DockGroupOrientation.Horizontal);
            this.left = group;

            if (this.Orientation == DockGroupOrientation.Undetermined)
            {
                this.Orientation = DockGroupOrientation.Horizontal;
            }

            return;
        }

        AppendToRoot(this.left, group, DockGroupOrientation.Horizontal);
    }

    public void DockTop(DockGroup group)
    {
        if (this.top == null)
        {
            this.AddBeforeCenter(group, DockGroupOrientation.Vertical);
            this.top = group;

            if (this.Orientation == DockGroupOrientation.Undetermined)
            {
                this.Orientation = DockGroupOrientation.Vertical;
            }

            return;
        }

        AppendToRoot(this.top, group, DockGroupOrientation.Vertical);
    }

    public void DockRight(DockGroup group)
    {
        if (this.right == null)
        {
            this.AddAfterCenter(group, DockGroupOrientation.Horizontal);
            this.right = group;

            if (this.Orientation == DockGroupOrientation.Undetermined)
            {
                this.Orientation = DockGroupOrientation.Horizontal;
            }

            return;
        }

        PrependToRoot(this.right, group, DockGroupOrientation.Horizontal);
    }

    public void DockBottom(DockGroup group)
    {
        if (this.bottom == null)
        {
            this.AddAfterCenter(group, DockGroupOrientation.Vertical);
            this.bottom = group;

            if (this.Orientation == DockGroupOrientation.Undetermined)
            {
                this.Orientation = DockGroupOrientation.Vertical;
            }

            return;
        }

        PrependToRoot(this.bottom, group, DockGroupOrientation.Vertical);
    }

    private static void PrependToRoot(DockGroup root, IDockGroup group, DockGroupOrientation orientation)
        => root.AddGroupFirst(group, orientation);

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

    private void AddBeforeCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent;
        Debug.Assert(parent is not null, "center always has a parent");
        parent.AddGroupBefore(group, this.center, orientation);
    }

    private void AddAfterCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent;
        Debug.Assert(parent is not null, "center always has a parent");
        parent.AddGroupAfter(group, this.center, orientation);
    }
}
