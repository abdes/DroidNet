// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a node in the binary tree structure that represents the layout of a docking workspace.
/// </summary>
/// <param name="docker">The workspace docker associated with the node.</param>
/// <param name="segment">The value stored in the node; corresponds to a layout segment in the workspace layout.</param>
/// <remarks>
/// The <see cref="DockingTreeNode"/> class extends <see cref="BinaryTreeNode{T}"/> to provide specialized behavior for managing layout segments within a docking workspace. It supports operations such as adding children, merging leaf parts, and repartitioning segments.
/// </remarks>
internal partial class DockingTreeNode(IDocker docker, LayoutSegment segment) : BinaryTreeNode<LayoutSegment>(segment)
{
    /// <summary>
    /// Gets or sets the left child node.
    /// </summary>
    /// <remarks>
    /// Redefined to allow for setting the property in derived classes (greatly simplifies unit testing).
    /// </remarks>
    public new virtual DockingTreeNode? Left
    {
        get => (DockingTreeNode?)base.Left;
        protected set => base.Left = value;
    }

    /// <summary>
    /// Gets or sets the right child node.
    /// </summary>
    /// <remarks>
    /// Redefined to allow for setting the property in derived classes (greatly simplifies unit testing).
    /// </remarks>
    public new virtual DockingTreeNode? Right
    {
        get => (DockingTreeNode?)base.Right;
        protected set => base.Right = value;
    }

    /// <summary>
    /// Gets the parent node.
    /// </summary>
    public new DockingTreeNode? Parent => (DockingTreeNode?)base.Parent;

    /// <inheritdoc/>
    public override DockingTreeNode? Sibling => (DockingTreeNode?)base.Sibling;

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be positioned on the left side of other segments in the node's subtree.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="orientation">The desired orientation of the segment after the node is added. Will be applied only if the segment has more than one child.</param>
    /// <remarks>
    /// This method ensures that the node can take a child or restructures it to make it so. It then adds the child to the left side, applying the desired orientation if necessary.
    /// </remarks>
    public void AddChildLeft(DockingTreeNode node, DockGroupOrientation orientation)
    {
        // Ensure that this node can take a children or restructure it to make it so.
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
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will be positioned on the right side of other segments in the node's subtree.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="orientation">The desired orientation of the segment after the node is added. Will be applied only if the segment has more than one child.</param>
    /// <remarks>
    /// This method ensures that the node can take a child or restructures it to make it so. It then adds the child to the right side, applying the desired orientation if necessary.
    /// </remarks>
    public void AddChildRight(DockingTreeNode node, DockGroupOrientation orientation)
    {
        // Ensure that this node can take a children or restructure it to make it so.
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
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will come <b>after</b> the specified <paramref name="sibling"/>.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="sibling">The sibling after which the node should be added.</param>
    /// <param name="orientation">The desired orientation of the containing flow of the added node when the docking tree is laid out.</param>
    /// <exception cref="InvalidOperationException">If the specified <paramref name="sibling"/> is not a child of this node.</exception>
    /// <remarks>
    /// This method may result in a restructuring of the node and its children. Although there are multiple structures that could achieve the same result, the post-condition that must be guaranteed is that if the subtree under the current node is flattened using a depth-first in-order traversal into a list, the newly added child will come immediately <b>after</b> the specified <paramref name="sibling"/>.
    /// </remarks>
    public void AddChildAfter(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
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
        if (this.Value is not EdgeGroup)
        {
            this.Value.Orientation = orientation;
        }
    }

    /// <summary>
    /// Adds a child to the node in such a way that in a depth-first in-order traversal, its layout segment will come <b>before</b> the specified <paramref name="sibling"/>.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <param name="sibling">The sibling after which the node should be added.</param>
    /// <param name="orientation">The desired orientation of the containing flow of the added node when the docking tree is laid out.</param>
    /// <exception cref="InvalidOperationException">If the specified <paramref name="sibling"/> is not a child of this node.</exception>
    /// <remarks>
    /// This method may result in a restructuring of the node and its children. Although there are multiple structures that could achieve the same result, the post-condition that must be guaranteed is that if the subtree under the current node is flattened using a depth-first in-order traversal into a list, the newly added child will come immediately <b>before</b> the specified <paramref name="sibling"/>.
    /// </remarks>
    public void AddChildBefore(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
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
        if (this.Value is not EdgeGroup)
        {
            this.Value.Orientation = orientation;
        }
    }

    /// <inheritdoc />
    /// <exception cref="InvalidOperationException">If the node to be removed holds the <see cref="CenterGroup"/> segment.</exception>
    /// <remarks>
    /// After the child is successfully removed, the node is left with at most one child, and its orientation is set to <see cref="DockGroupOrientation.Undetermined"/> (this does not apply to edge segments, which orientation never changes).
    /// </remarks>
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

    /// <inheritdoc />
    public override string ToString() => $"{base.ToString()} \u2191 {this.Parent?.Value.DebugId}";

    /// <summary>
    /// Merge two leaf children with compatible orientations together, leaving a single child as a result.
    /// </summary>
    /// <exception cref="InvalidOperationException">When one of the children is <see langword="null"/>, or is a leaf node, or holds the <see cref="CenterGroup"/>.</exception>
    /// <remarks>
    /// This operation is useful when a node has two leaf children, both of them hold segments with compatible orientations. The docks they hold will be merged in a single segment, held by the <see cref="Left"/> child of this node, and the second child will be removed. The resulting tree will be simpler.
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
        if (this.Left.Value is LayoutDockGroup segment)
        {
            segment.Orientation = segment.Docks.Count > 1 ? this.Value.Orientation : DockGroupOrientation.Undetermined;
        }

        this.RemoveChild(this.Right);
    }

    /// <summary>
    /// Restructure the docks in this node's segment to partition them into three groups. The first group will hold any docks before the <paramref name="relativeTo"/> dock, the second will hold the <paramref name="relativeTo"/> dock, and the third will hold the docks after the <paramref name="relativeTo"/> dock.
    /// </summary>
    /// <param name="relativeTo">The dock relative to which the partitioning will happen.</param>
    /// <param name="orientation">The desired orientation of the resulting subtree root.</param>
    /// <returns>The segment containing the <paramref name="relativeTo"/>.</returns>
    /// <exception cref="InvalidOperationException">If this node does not hold a <see cref="LayoutDockGroup"/> segment or if the <paramref name="relativeTo"/> does not belong to this node's segment.</exception>
    /// <remarks>
    /// This method partitions the docks in the node's segment into three groups based on their position relative to the specified dock. It then restructures the node to reflect this partitioning.
    /// </remarks>
    internal LayoutDockGroup Repartition(IDock? relativeTo, DockGroupOrientation orientation)
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

        try
        {
            this.Left = beforeNode ?? hostNode;
            if (beforeNode is null)
            {
                this.Right = afterNode;
                afterNode = null; // Dispose ownership transferred
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
                afterNode = null; // Dispose ownership transferred
            }
        }
        finally
        {
            afterNode?.Dispose();
        }

        // We no longer have docks, so we switch the existing item in the node with a new LayoutGroup
        this.Value = new LayoutGroup(docker, this.Value.Orientation);

        return hostGroup;
    }

    /// <summary>
    /// Migrate the content of a lone child into this node then remove the child from the node.
    /// </summary>
    /// <param name="child">The child to be assimilated.</param>
    /// <exception cref="InvalidOperationException">When the <paramref name="child"/> has a sibling, or is a root group (<see cref="CenterGroup"/> or <see cref="EdgeGroup"/>), or the specified child is not a child of this node.</exception>
    /// <remarks>
    /// This operation is useful when the child is not needed to ensure the expected layout (typically, a single child with a compatible orientation with its parent orientation). The resulting tree after this operation will be simpler.
    /// </remarks>
    internal void AssimilateChild(DockingTreeNode child)
    {
        if (child.Sibling is not null)
        {
            throw new InvalidOperationException("only a lone child can be assimilated by its parent");
        }

        if (child.Value is CenterGroup or EdgeGroup)
        {
            throw new InvalidOperationException("root groups cannot be assimilated");
        }

        if (this.Left != child && this.Right != child)
        {
            throw new InvalidOperationException($"node ({child}) is not managed by me ({this})");
        }

        if (child.IsLeaf)
        {
            this.Left = this.Right = null;
            this.MigrateDocks(from: child, to: this);
        }
        else
        {
            this.Left = child.Left;
            this.Right = child.Right;
        }

        if (child.Value.Orientation != DockGroupOrientation.Undetermined)
        {
            this.Value.Orientation = child.Value.Orientation;
        }
    }

    /// <summary>
    /// Migrates docks from one node to another.
    /// </summary>
    /// <param name="from">The source docking node.</param>
    /// <param name="to">The target docking node.</param>
    /// <exception cref="InvalidOperationException">If the target node holds a <see cref="LayoutGroup"/> and has children or holds a segment of any other type than <see cref="LayoutGroup"/> or <see cref="LayoutDockGroup"/>.</exception>
    /// <remarks>
    /// If the target node contains a <see cref="LayoutDockGroup"/>, docks from the source node are migrated into it. Otherwise, if it holds a <see cref="LayoutGroup"/> and has no children, its segment is replaced with the source's segment and the source's segment is replaced with a new <see cref="LayoutGroup"/>.
    /// <para>
    /// This operation has no effect if the source node does not hold a <see cref="LayoutDockGroup"/>.
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
    /// Create a new subtree having as a right child a node holding this node's segment, and as a left child the specified <paramref name="node"/>.
    /// </summary>
    /// <param name="node">The node to be added after expansion.</param>
    /// <returns>The resulting subtree.</returns>
    /// <remarks>
    /// This method does not modify the docking tree or this node. The resulting subtree needs to be plugged into the tree after the expansion.
    /// </remarks>
    private DockingTreeNode ExpandToAddLeft(DockingTreeNode node)
        => new(docker, new LayoutGroup(docker))
        {
            Left = node,
            Right = new DockingTreeNode(docker, this.Value),
        };

    /// <summary>
    /// Create a new subtree having as a left child a node holding this node's segment, and as a right child the specified
    /// <paramref name="node" />.
    /// </summary>
    /// <param name="node">The node to be added after expansion.</param>
    /// <returns>The resulting subtree.</returns>
    /// <remarks>
    /// This method does not modify the docking tree or this node. The resulting subtree needs to be plugged into the tree after
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
