// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Utils;

using System.Diagnostics;

internal class TreeNode
{
    private readonly List<TreeNode> children = [];

    protected TreeNode? Parent { get; private set; }

    protected IReadOnlyCollection<TreeNode> Children
        => this.children.AsReadOnly();

    protected IReadOnlyCollection<TreeNode> Siblings
        => this.Parent is null
            ? Array.Empty<TreeNode>().AsReadOnly()
            : this.Parent.children.Where(c => c != this).ToList().AsReadOnly();

    protected TreeNode Root
    {
        get
        {
            var node = this;
            while (node.Parent != null)
            {
                node = node.Parent;
            }

            return node;
        }
    }

    public void ClearChildren() => this.children.Clear();

    protected bool RemoveChild(TreeNode node)
    {
        Debug.Assert(
            node is not null,
            "nodes in a Tree<T> must all be implemented by TreeNode<T>");

        if (!this.children.Remove(node))
        {
            return false;
        }

        node.Parent = null;
        return true;
    }

    protected void AddSibling(TreeNode node)
    {
        if (this.Parent is null)
        {
            throw new InvalidOperationException("cannot add a sibling to a node that does not have a parent");
        }

        this.Parent.AddChild(node);
    }

    protected bool RemoveSibling(TreeNode node) => this.Parent?.RemoveChild(node) == true;

    protected void MoveTo(TreeNode newParent)
    {
        if (this.Parent == null)
        {
            throw new InvalidOperationException("cannot move the root node.");
        }

        var removed = this.Parent.RemoveChild(this);
        Debug.Assert(
            removed,
            "node with a non-null parent was not found in its parent children");

        newParent.AddChild(this);
    }

    protected void AddChild(TreeNode node)
    {
        this.children.Add(node);
        node.Parent = this;
    }
}
