// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics;
using DroidNet.Docking;

/// <summary>Represents a node in the binary tree structure that represents the layout of a docking workspace.</summary>
/// <param name="docker">The workspace docker associated with the node.</param>
/// <param name="segment">The value stored in the node; corresponds to a layout segment in the workspace layout.</param>
internal class DockingTreeNode(IDocker docker, LayoutSegment segment) : BinaryTreeNode<LayoutSegment>(segment)
{
    public new DockingTreeNode? Left { get => (DockingTreeNode?)base.Left; protected set => base.Left = value; }

    public new DockingTreeNode? Right { get => (DockingTreeNode?)base.Right; protected set => base.Right = value; }

    public new DockingTreeNode? Parent => (DockingTreeNode?)base.Parent;

    public override DockingTreeNode? Sibling => (DockingTreeNode?)base.Sibling;

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be positioned on
    /// the left side of other segment in the node's sub-tree.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="orientation">The desired orientation of the segment after the node is added. Will be applied only if the
    /// segment has more than one child.</param>
    public void AddChildLeft(DockingTreeNode node, DockGroupOrientation orientation)
    {
        // Ensure that this node can take a children or resturcture it to make it so.
        this.EnsureInternalNode();

        if (this.Left is null)
        {
            // Use the left slot.
            this.Left = node;

            // Apply the desired orientation, but only if the segment has more than one child.
            if (this.Value is not EdgeGroup && this.Right != null)
            {
                this.Value.Orientation = orientation;
            }

            return;
        }

        if (this.Right is null)
        {
            // Add to right then swap the left and right slots.
            this.Right = node;
            this.SwapLeftAndRight();

            // Apply the desired orientation.
            if (this.Value is not EdgeGroup)
            {
                this.Value.Orientation = orientation;
            }

            return;
        }

        // Add to the left child.
        this.Left.AddChildLeft(node, orientation);
    }

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be positioned on
    /// the right side of other segment in the node's sub-tree.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="orientation">The desired orientation of the segment after the node is added. Will be applied only if the
    /// segment has more than one child.</param>
    public void AddChildRight(DockingTreeNode node, DockGroupOrientation orientation)
    {
        // Ensure that this node can take a children or resturcture it to make it so.
        this.EnsureInternalNode();

        if (this.Right is null)
        {
            // Use the right slot.
            this.Right = node;

            // Apply the desired orientation, but only if the segment has more than one child.
            if (this.Value is not EdgeGroup && this.Left != null)
            {
                this.Value.Orientation = orientation;
            }

            return;
        }

        if (this.Left is null)
        {
            // Add to left then swap the left and right slots.
            this.Left = node;
            this.SwapLeftAndRight();

            // Apply the desired orientation.
            if (this.Value is not EdgeGroup)
            {
                this.Value.Orientation = orientation;
            }

            return;
        }

        // Add to the right child.
        this.Right.AddChildRight(node, orientation);
    }

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be come
    /// <b>after</b> the specified <paramref name="sibling" />.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="sibling">The sibling after which the node should be added.</param>
    /// <param name="orientation">The desired orientation of the containing flow of the added node when the docking tree is laid
    /// out.</param>
    /// <exception cref="InvalidOperationException">If the specified <paramref name="sibling" /> is not a child of this
    /// node.</exception>
    /// <remarks>
    /// This method may result in a restructuring of the node and its children. Although there are multiple structures that could
    /// achieve the same result, the post-condition that must be guaranteed is that if the sub-tree under the current node is
    /// flattened using a depth-first in-order traversal into a list, the newly added child will come immediatley <b>after</b> the
    /// specified <paramref name="sibling" />.
    /// </remarks>
    public void AddChildAfter(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling, then it must be a layout node.
        Debug.Assert(this.Value is LayoutGroup, "a node with children must be a layout node");

        if (this.Value.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Value.Orientation = orientation;
        }

        if (this.Left != null && this.Right != null)
        {
            if (this.Left == sibling)
            {
                this.Left = this.Left.ExpandToAddRight(node);
                this.Left.Value.Orientation = orientation;
            }
            else if (this.Right == sibling)
            {
                this.Right = this.Right.ExpandToAddRight(node);
                this.Right.Value.Orientation = orientation;
            }

            return;
        }

        if (this.Right is not null)
        {
            this.SwapLeftAndRight();
        }

        this.Right = node;
        this.Value.Orientation = orientation;
    }

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be come
    /// <b>before</b> the specified <paramref name="sibling" />.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="sibling">The sibling after which the node should be added.</param>
    /// <param name="orientation">The desired orientation of the containing flow of the added node when the docking tree is laid
    /// out.</param>
    /// <exception cref="InvalidOperationException">If the specified <paramref name="sibling" /> is not a child of this
    /// node.</exception>
    /// <remarks>
    /// This method may result in a restructuring of the node and its children. Although there are multiple structures that could
    /// achieve the same result, the post-condition that must be guaranteed is that if the sub-tree under the current node is
    /// flattened using a depth-first in-order traversal into a list, the newly added child will come immediatley <b>before</b> the
    /// specified <paramref name="sibling" />.
    /// </remarks>
    public void AddChildBefore(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling, then it must be a layout node.
        Debug.Assert(this.Value is LayoutGroup, "a node with children must be a layout node");

        if (this.Value.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Value.Orientation = orientation;
        }

        if (this.Left != null && this.Right != null)
        {
            if (this.Left == sibling)
            {
                this.Left = this.Left.ExpandToAddLeft(node);
                this.Left.Value.Orientation = orientation;
            }
            else if (this.Right == sibling)
            {
                this.Right = this.Right.ExpandToAddLeft(node);
                this.Right.Value.Orientation = orientation;
            }

            return;
        }

        if (this.Left is not null)
        {
            this.SwapLeftAndRight();
        }

        this.Left = node;
        this.Value.Orientation = orientation;
    }

    /// <inheritdoc />
    /// <exception cref="InvalidOperationException">If the node to be removed holds the <see cref="CenterGroup" />
    /// segment.</exception>
    public override void RemoveChild(BinaryTreeNode<LayoutSegment> node)
    {
        if (node.Value is CenterGroup)
        {
            // We cannot remove the center group.
            throw new InvalidOperationException($"the center group cannot be removed from `{this}`");
        }

        base.RemoveChild(node);

        // The node should be left with a single child now and its orientation should be set to Undetermined, unless it
        // is an edge group. Edge group's orientation cannot change once it is set.
        if (this.Value is not EdgeGroup)
        {
            this.Value.Orientation = DockGroupOrientation.Undetermined;
        }
    }

    /// <summary>Migrate then remove the specified child into this node then remove the child from the node.</summary>
    /// <param name="child">The child to be assimilated.</param>
    /// <remarks>
    /// This operation is useful when the child is not needed to ensure the expected layout (typically, a single child with a
    /// compatible orientation with its parent orientation). The resulting tree after this operation will be simpler.
    /// </remarks>
    public void AssimilateChild(DockingTreeNode child)
    {
        Debug.Assert(this.Left is null || this.Right is null, "only a lone child can be assimilated by its parent");
        Debug.Assert(child.Value is not CenterGroup or EdgeGroup, "root groups cannot be assimilated");
        Debug.Assert(this.Value is LayoutGroup, $"only nodes with a {nameof(LayoutGroup)} item can assimilate a child");

        if (child.IsLeaf)
        {
            this.Left = this.Right = null;
            this.MigrateDocks(from: child, to: this);
        }
        else
        {
            Debug.Assert(this.Value is LayoutGroup, $"internal node must have an item with type{nameof(LayoutGroup)}");
            this.Left = child.Left;
            this.Right = child.Right;
        }

        if (child.Value.Orientation != DockGroupOrientation.Undetermined)
        {
            this.Value.Orientation = child.Value.Orientation;
        }
    }

    /// <summary>Merge two leaf children with compatible orientations together, leaving a single child as a result.</summary>
    /// <exception cref="InvalidOperationException">When one of the children is <see langword="null" />, or is a leaf node, or
    /// holds the <see cref="CenterGroup" />.</exception>
    /// <remarks>
    /// This operation is useful when a node has two leaf children, both of them hold segments with compatible orientations. The
    /// docks they hold will be merged in a single segment, held by the <see cref="Left" /> child of this node, and second child
    /// will be removed. The resulting tree will be simpler.
    /// </remarks>
    public void MergeLeafParts()
    {
        if (this.Left is null || this.Right is null)
        {
            throw new InvalidOperationException("cannot merge null parts");
        }

        if (!this.Left.IsLeaf || !this.Right.IsLeaf)
        {
            throw new InvalidOperationException("can only merge parts if both parts are leaves");
        }

        if (this.Left.Value is CenterGroup || this.Right.Value is CenterGroup)
        {
            throw new InvalidOperationException(
                $"cannot merge parts when one of them is the center group: {(this.Left.Value is CenterGroup ? this.Left : this.Right)}");
        }

        this.MigrateDocks(from: this.Right, to: this.Left);
        this.Left.Value.Orientation = this.Left.Parent!.Value.Orientation;
        this.RemoveChild(this.Right);
    }

    /// <summary>Restructure the docks in this node's segment to partition them into theree groups. The first group will hold any
    /// docks before the <paramref name="relativeTo" /> dock, the second will hold the <paramref name="relativeTo" /> dock, and the
    /// third will hold the docks after the <paramref name="relativeTo" /> dock.</summary>
    /// <param name="relativeTo">The dock relative to which the partitioning will happen.</param>
    /// <param name="orientation">The desired orientation of the resulting sub-tree root.</param>
    /// <returns>The segment containing the <paramref name="relativeTo" />.</returns>
    /// <exception cref="InvalidOperationException">If this not does not hold a <see cref="LayoutDockGroup" /> segment.</exception>
    public LayoutDockGroup Repartition(IDock? relativeTo, DockGroupOrientation orientation)
    {
        if (this.Value is not LayoutDockGroup group)
        {
            throw new InvalidOperationException(
                $"cannot repartition {this} because it does not hold a segment of type {nameof(LayoutDockGroup)}");
        }

        var (beforeGroup, hostGroup, afterGroup) = group.Split(relativeTo, orientation);

        var beforeNode = beforeGroup is null ? null : new DockingTreeNode(docker, beforeGroup);
        var hostNode = new DockingTreeNode(docker, hostGroup);
        var afterNode = afterGroup is null ? null : new DockingTreeNode(docker, afterGroup);

        this.Left = beforeNode ?? hostNode;
        if (beforeNode is null)
        {
            this.Right = afterNode;
        }
        else if (afterNode is null)
        {
            this.Right = hostNode;
        }
        else
        {
            this.Right = new DockingTreeNode(docker, new LayoutGroup(docker, this.Value.Orientation))
            {
                Left = hostNode,
                Right = afterNode,
            };
        }

        // We no longer have docks, so we switch the existing item in the node with a new LayoutGroup
        this.Value = new LayoutGroup(docker, this.Value.Orientation);

        return hostGroup;
    }

    /// <inheritdoc />
    public override string ToString() => $"{base.ToString()} \u2191 {this.Parent?.Value.DebugId}";

    /// <summary>Migrates docks from one node to another.</summary>
    /// <param name="from">The source docking node.</param>
    /// <param name="to">The target docking node.</param>
    /// <exception cref="InvalidOperationException">If the target node holds a <see cref="LayoutGroup" /> and has children or
    /// holds a segments of any other type than <see cref="LayoutGroup" /> or <see cref="LayoutDockGroup" />.</exception>
    /// <remarks>
    /// If the target node contains a <see cref="LayoutDockGroup" />, docks from the source node are migrated into it. Otherwise,
    /// if it holds a <see cref="LayoutGroup" /> and has no children, its segment is replaced with the source's segment and the
    /// source's segment is replaced with a new <see cref="LayoutGroup" />.
    /// <para>
    /// This operation has no effect if the source node does not hold a <see cref="LayoutDockGroup" />.
    /// </para>
    /// </remarks>
    private void MigrateDocks(DockingTreeNode from, DockingTreeNode to)
    {
        if (from.Value is LayoutDockGroup fromGroup)
        {
            if (to.Value is LayoutDockGroup toGroup)
            {
                fromGroup.MigrateDocks(toGroup);
            }
            else if (to.Value is LayoutGroup)
            {
                if (!to.IsLeaf)
                {
                    throw new InvalidOperationException($"cannot migrate docks to a node ({to}) with children.");
                }

                to.Value = fromGroup;
                from.Value = new LayoutGroup(docker);
            }
            else
            {
                throw new InvalidOperationException(
                    $"cannot migrate to a node ({to}) that holds a {to.Value.GetType().Name}");
            }
        }
    }

    /// <summary>
    /// Create a new sub-tree having as a right child a node holding this node's segment, and as a left child the specified
    /// <paramref name="node" />.
    /// </summary>
    /// <param name="node">The node to be added after expansion.</param>
    /// <returns>The resulting sub-tree.</returns>
    /// <remarks>
    /// This method does not modify the docking tree or this node. The resulting sub-tree needs to be plugged into the tree after
    /// the expansion.
    /// </remarks>
    private DockingTreeNode ExpandToAddLeft(DockingTreeNode node)
        => new(docker, new LayoutGroup(docker))
        {
            Left = node,
            Right = new DockingTreeNode(docker, this.Value),
        };

    /// <summary>
    /// Create a new sub-tree having as a left child a node holding this node's segment, and as a right child the specified
    /// <paramref name="node" />.
    /// </summary>
    /// <param name="node">The node to be added after expansion.</param>
    /// <returns>The resulting sub-tree.</returns>
    /// <remarks>
    /// This method does not modify the docking tree or this node. The resulting sub-tree needs to be plugged into the tree after
    /// the expansion.
    /// </remarks>
    private DockingTreeNode ExpandToAddRight(DockingTreeNode node)
        => new(docker, new LayoutGroup(docker))
        {
            Left = new DockingTreeNode(docker, this.Value),
            Right = node,
        };

    /// <summary>
    /// Checks that this node is an internal node (holds a LayoutGroup), otherwise transforms it into one and moves its value to a
    /// new child placed in its left slot.
    /// </summary>
    /// <exception cref="InvalidOperationException">If this node holds the <see cref="CenterGroup" />, which cannot be an internal
    /// node.</exception>
    private void EnsureInternalNode()
    {
        if (this.Value is LayoutGroup)
        {
            return;
        }

        if (this.Value is CenterGroup)
        {
            throw new InvalidOperationException(
                "the docking tree node containing the center group can only be a leaf node");
        }

        Debug.Assert(
            this.Left is null && this.Right is null,
            $"a node that does not hold a {nameof(LayoutGroup)} item cannot have children");

        // Transform the node into a layout node.
        this.Left = new DockingTreeNode(docker, this.Value);
        this.Value = new LayoutGroup(docker);
    }
}
