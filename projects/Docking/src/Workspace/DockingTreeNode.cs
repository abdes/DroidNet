// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics;
using DroidNet.Docking;

internal sealed class DockingTreeNode(IDocker docker, LayoutSegment item) : IDisposable
{
    private bool disposed;

    private DockingTreeNode? left;
    private DockingTreeNode? right;

    public LayoutSegment Item { get; private set; } = item;

    public DockingTreeNode? Left
    {
        get => this.left;
        private set
        {
            this.left = value;
            if (value != null)
            {
                value.Parent = this;
            }
        }
    }

    public DockingTreeNode? Right
    {
        get => this.right;
        private set
        {
            this.right = value;
            if (value != null)
            {
                value.Parent = this;
            }
        }
    }

    public DockingTreeNode? Parent { get; private set; }

    public DockingTreeNode? Sibling
    {
        get
        {
            if (this.Parent is null)
            {
                return null;
            }

            return this.Parent.Left == this ? this.Parent.Right : this.Parent.Left;
        }
    }

    public bool IsLeaf => this.Left is null && this.right is null;

    public void AddChildLeft(DockingTreeNode item)
    {
        this.EnsureInternalNode();
        if (this.Left is not null)
        {
            if (this.right is null)
            {
                this.SwapLeftAndRight();
            }
            else
            {
                this.ExpandLeft(ref this.left!);
                this.Left.AddChildLeft(item);
                return;
            }
        }

        this.Left = item;
    }

    public void AddChildRight(DockingTreeNode item)
    {
        this.EnsureInternalNode();
        if (this.right is not null)
        {
            if (this.Left is null)
            {
                this.SwapLeftAndRight();
            }
            else
            {
                this.ExpandRight(ref this.right);
                Debug.Assert(this.right is not null, "because it was has just been expanded");
                this.right.AddChildRight(item);
                return;
            }
        }

        this.Right = item;
    }

    /// <summary>
    /// Dump to the contents of a docking tree, starting at the this node, to the Debug output.
    /// </summary>
    /// <param name="indentChar">The character used to indent children relative to their parent. Default is <c>' '</c>.</param>
    /// <param name="indentSize">The number of indent characters to use per indentation level. Default is <c>3</c>.</param>
    /// <param name="initialIndentLevel">Can be used to specify an initial indentation for the dumped info.</param>
    public void Dump(
        char indentChar = ' ',
        int indentSize = 3,
        int initialIndentLevel = 0)
        => DumpRecursive(this, initialIndentLevel, indentChar, indentSize);

    public override string ToString()
    {
        string[] children =
        [
            this.Left is null ? string.Empty : $"{this.Left.Item}",
            this.right is null ? string.Empty : $"{this.right.Item}",
        ];

        var childrenStr = children[0] != string.Empty || children[1] != string.Empty
            ? $" {{{string.Join(',', children)}}}"
            : string.Empty;
        return $"{this.Item}{childrenStr}";
    }

    public void RemoveChild(DockingTreeNode node)
    {
        if (node.Item is CenterGroup)
        {
            // We cannot remove the center group, so just return.
            return;
        }

        var mine = false;
        if (this.Left == node)
        {
            mine = true;
            this.Left = null;
        }
        else if (this.Right == node)
        {
            mine = true;
            this.Right = null;
        }

        Debug.Assert(mine, $"do not call {nameof(this.RemoveChild)} with a node that is not actually a child");

        if (this.Item is not EdgeGroup)
        {
            this.Item.Orientation = DockGroupOrientation.Undetermined;
        }
    }

    public void AssimilateChild(DockingTreeNode child)
    {
        Debug.Assert(this.Left is null || this.right is null, "only a lone child can be assimilated by its parent");
        Debug.Assert(child.Item is not CenterGroup or EdgeGroup, "root groups cannot be assimilated");
        Debug.Assert(this.Item is LayoutGroup, $"only nodes with a {nameof(LayoutGroup)} item can assimilate a child");

        if (child.IsLeaf)
        {
            this.MigrateDocks(from: child, to: this);
            this.Left = this.Right = null;
        }
        else
        {
            Debug.Assert(this.Item is LayoutGroup, $"internal node must have an item with type{nameof(LayoutGroup)}");
            this.Left = child.left;
            this.Right = child.right;
        }

        if (child.Item.Orientation != DockGroupOrientation.Undetermined)
        {
            this.Item.Orientation = child.Item.Orientation;
        }

        child.Parent = null;
    }

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        if (this.Item is IDisposable resource)
        {
            resource.Dispose();
        }

        this.Left?.Dispose();
        this.right?.Dispose();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    internal void MergeLeafParts()
    {
        var firstChild = this.Left;
        var secondChild = this.Right;

        if (firstChild is null || secondChild is null)
        {
            throw new InvalidOperationException("cannot merge null parts");
        }

        if (!firstChild.IsLeaf || !secondChild.IsLeaf)
        {
            throw new InvalidOperationException("can only merge parts if both parts are leaves");
        }

        if (firstChild.Item is CenterGroup || secondChild.Item is CenterGroup)
        {
            throw new InvalidOperationException(
                $"cannot merge parts when one of them is the center group: {(firstChild.Item is CenterGroup ? firstChild : secondChild)}");
        }

        this.MigrateDocks(from: secondChild, to: firstChild);
        firstChild.Item.Orientation = firstChild.Parent!.Item.Orientation;
        this.RemoveChild(secondChild);
    }

    internal LayoutDockGroup Repartition(
        LayoutDockGroup group,
        IDock? relativeTo,
        DockGroupOrientation requiredOrientation)
    {
        var (beforeGroup, hostGroup, afterGroup) = group.Split(relativeTo, requiredOrientation);

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
            this.Right = new DockingTreeNode(docker, new LayoutGroup(docker, this.Item.Orientation))
            {
                Left = hostNode,
                Right = afterNode,
            };
        }

        // We no longer have docks, so we switch the existing item in the node with a new LayoutGroup
        this.Item = new LayoutGroup(docker, this.Item.Orientation);

        return hostGroup;
    }

    internal void AddChildBefore(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        void ExpandAndAddLeft(ref DockingTreeNode part)
        {
            this.ExpandLeft(ref part);
            Debug.Assert(part is not null, "because it was has just been expanded");
            part.AddChildLeft(node);
            part.Item.Orientation = orientation;
        }

        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling, then it must be a layout node.
        Debug.Assert(this.Item is LayoutGroup, "a node with children must be a layout node");

        if (this.Item.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Item.Orientation = orientation;
        }

        if (this.Left != null && this.right != null)
        {
            if (this.Left == sibling)
            {
                ExpandAndAddLeft(ref this.left!);
            }
            else if (this.right == sibling)
            {
                ExpandAndAddLeft(ref this.right!);
            }

            return;
        }

        if (this.Left is not null)
        {
            this.SwapLeftAndRight();
        }

        this.Left = node;
        this.Item.Orientation = orientation;
    }

    internal void AddChildAfter(DockingTreeNode node, DockingTreeNode sibling, DockGroupOrientation orientation)
    {
        void ExpandAndAddRight(ref DockingTreeNode part)
        {
            this.ExpandRight(ref part);
            Debug.Assert(part is not null, "because it was has just been expanded");
            part.AddChildRight(node);
            part.Item.Orientation = orientation;

            // part.ConsolidateUp();
        }

        if (this.Left != sibling && this.Right != sibling)
        {
            throw new InvalidOperationException($"relative node ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling, then it must be a layout node.
        Debug.Assert(this.Item is LayoutGroup, "a node with children must be a layout node");

        if (this.Item.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Item.Orientation = orientation;
        }

        if (this.Left != null && this.right != null)
        {
            if (this.Left == sibling)
            {
                ExpandAndAddRight(ref this.left!);
            }
            else if (this.right == sibling)
            {
                ExpandAndAddRight(ref this.right!);
            }

            return;
        }

        if (this.right is not null)
        {
            this.SwapLeftAndRight();
        }

        this.Right = node;
        this.Item.Orientation = orientation;
    }

    private static void DumpRecursive(DockingTreeNode? node, int indentLevel, char indentChar, int indentSize)
    {
        if (node is null)
        {
            return;
        }

        var indent = new string(indentChar, indentLevel * indentSize); // 2 spaces per indent level
        Debug.WriteLine($"{indent}{node.Item}");
        DumpRecursive(node.Left, indentLevel + 1, indentChar, indentSize);
        DumpRecursive(node.Right, indentLevel + 1, indentChar, indentSize);
    }

    private void MigrateDocks(DockingTreeNode from, DockingTreeNode to)
    {
        if (from.Item is LayoutDockGroup fromGroup)
        {
            if (to.Item is LayoutGroup)
            {
                to.Item = fromGroup;
                from.Item = new LayoutGroup(docker);
            }
            else if (to.Item is LayoutDockGroup toGroup)
            {
                fromGroup.MigrateDocks(toGroup);
            }
        }
    }

    private void ExpandLeft(ref DockingTreeNode node)
    {
        var newNode = new DockingTreeNode(docker, new LayoutGroup(docker))
        {
            Parent = node.Parent,
        };
        newNode.AddChildRight(node);
        node = newNode;
    }

    private void ExpandRight(ref DockingTreeNode node)
    {
        var newNode = new DockingTreeNode(docker, new LayoutGroup(docker))
        {
            Parent = node.Parent,
        };
        newNode.AddChildLeft(node);
        node = newNode;
    }

    private void SwapLeftAndRight() => (this.Left, this.right) = (this.right, this.Left);

    private void EnsureInternalNode()
    {
        if (this.Item is LayoutGroup)
        {
            return;
        }

        if (this.Item is CenterGroup)
        {
            throw new InvalidOperationException(
                "the docking tree node containing the center group can only be a leaf node");
        }

        Debug.Assert(
            this.Left is null && this.right is null,
            $"a node that does not hold a {nameof(LayoutGroup)} item cannot have children");

        // Transform the node into a layout node
        this.Left = new DockingTreeNode(docker, this.Item)
        {
            Parent = this,
        };
        this.Item = new LayoutGroup(docker);
    }
}
