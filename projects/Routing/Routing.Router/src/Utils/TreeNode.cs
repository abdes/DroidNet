// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Routing.Utils;

/// <summary>
/// Represents a node in a tree structure, typically used for hierarchical data.
/// </summary>
/// <remarks>
/// The <see cref="TreeNode"/> class is a fundamental building block for representing hierarchical data structures.
/// Each node can have a parent and multiple children, allowing the construction of complex tree structures.
/// </remarks>
internal class TreeNode
{
    private readonly List<TreeNode> children = [];

    /// <summary>
    /// Gets the parent node of this tree node.
    /// </summary>
    /// <value>
    /// The parent <see cref="TreeNode"/> of this node, or <see langword="null"/> if this node is the root.
    /// </value>
    protected TreeNode? Parent { get; private set; }

    /// <summary>
    /// Gets the collection of child nodes of this tree node.
    /// </summary>
    /// <value>
    /// A read-only collection of child <see cref="TreeNode"/> instances.
    /// </value>
    protected IReadOnlyCollection<TreeNode> Children => this.children.AsReadOnly();

    /// <summary>
    /// Gets the collection of sibling nodes of this tree node.
    /// </summary>
    /// <value>
    /// A read-only collection of sibling <see cref="TreeNode"/> instances, excluding this node.
    /// </value>
    protected IReadOnlyCollection<TreeNode> Siblings => this.Parent is null
        ? Array.Empty<TreeNode>().AsReadOnly()
        : this.Parent.children.Where(c => c != this).ToList().AsReadOnly();

    /// <summary>
    /// Gets the root node of the tree to which this node belongs.
    /// </summary>
    /// <value>
    /// The root <see cref="TreeNode"/> of the tree.
    /// </value>
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

    /// <summary>
    /// Clears all child nodes from this tree node.
    /// </summary>
    /// <remarks>
    /// This method removes all child nodes from the children collection of this node.
    /// </remarks>
    public void ClearChildren() => this.children.Clear();

    /// <summary>
    /// Removes a specified child node from this tree node.
    /// </summary>
    /// <param name="node">The child node to remove.</param>
    /// <returns><see langword="true"/> if the child node was successfully removed; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method removes the specified child node from the children collection of this node and sets the parent property of the child node to <see langword="null"/>.
    /// </remarks>
    protected bool RemoveChild(TreeNode node)
    {
        Debug.Assert(node is not null, "nodes in a Tree<T> must all be implemented by TreeNode<T>");

        if (!this.children.Remove(node))
        {
            return false;
        }

        node.Parent = null;
        return true;
    }

    /// <summary>
    /// Adds a sibling node to this tree node.
    /// </summary>
    /// <param name="node">The sibling node to add.</param>
    /// <remarks>
    /// This method adds the specified sibling node to the parent of this node. If this node does not have a parent, an <see cref="InvalidOperationException"/> is thrown.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if this node does not have a parent.</exception>
    protected void AddSibling(TreeNode node)
    {
        if (this.Parent is null)
        {
            throw new InvalidOperationException("cannot add a sibling to a node that does not have a parent");
        }

        this.Parent.AddChild(node);
    }

    /// <summary>
    /// Removes a specified sibling node from this tree node.
    /// </summary>
    /// <param name="node">The sibling node to remove.</param>
    /// <returns><see langword="true"/> if the sibling node was successfully removed; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method removes the specified sibling node from the parent of this node. If this node does not have a parent, the method returns <see langword="false"/>.
    /// </remarks>
    protected bool RemoveSibling(TreeNode node) => this.Parent?.RemoveChild(node) == true;

    /// <summary>
    /// Moves this tree node to a new parent node.
    /// </summary>
    /// <param name="newParent">The new parent node.</param>
    /// <remarks>
    /// This method removes this node from its current parent and adds it to the specified new parent node.
    /// If this node is the root node, an <see cref="InvalidOperationException"/> is thrown.
    /// </remarks>
    /// <exception cref="InvalidOperationException">Thrown if this node is the root node.</exception>
    protected void MoveTo(TreeNode newParent)
    {
        if (this.Parent == null)
        {
            throw new InvalidOperationException("cannot move the root node.");
        }

        var removed = this.Parent.RemoveChild(this);
        Debug.Assert(removed, "node with a non-null parent was not found in its parent children");

        newParent.AddChild(this);
    }

    /// <summary>
    /// Adds a child node to this tree node.
    /// </summary>
    /// <param name="node">The child node to add.</param>
    /// <remarks>
    /// This method adds the specified child node to the children collection of this node and sets the parent property of the child node to this node.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var parent = new TreeNode();
    /// var child = new TreeNode();
    /// parent.AddChild(child);
    /// ]]></code>
    /// </para>
    /// </remarks>
    protected void AddChild(TreeNode node)
    {
        this.children.Add(node);
        node.Parent = this;
    }
}
