// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a node in a binary tree, which can store a value of type <typeparamref name="T"/> and can optionally have a left
/// and a right child.
/// </summary>
/// <typeparam name="T">The type of the value held by this node.</typeparam>
/// <param name="storedValue">The initial value stored by this node.</param>
/// <remarks>
/// The <see cref="BinaryTreeNode{T}"/> class provides a flexible structure for representing hierarchical
/// data. Each node can have a left and right child, and can be part of a larger tree structure.
/// </remarks>
internal partial class BinaryTreeNode<T>(T storedValue) : IDisposable
{
    private bool disposed;

    private BinaryTreeNode<T>? left;
    private BinaryTreeNode<T>? right;
    private BinaryTreeNode<T>? parent;

    /// <summary>
    /// Gets or sets the value stored in this node.
    /// </summary>
    /// <value>
    /// The value of type <typeparamref name="T"/> stored in this node.
    /// </value>
    public T Value { get; protected set; } = storedValue;

    /// <summary>
    /// Gets or sets the left child node. Automatically manages the <see cref="Parent"/> association.
    /// </summary>
    /// <value>
    /// The left child node, or <see langword="null"/> if there is no left child.
    /// </value>
    public virtual BinaryTreeNode<T>? Left
    {
        get => this.left;

        // Property setter should be used for automatic management of parent relationship.
        protected set
        {
            if (this.left == value)
            {
                return;
            }

            if (this.left is not null)
            {
                this.left.parent = null;
            }

            this.left = value;

            if (value is not null)
            {
                value.parent = this;
            }
        }
    }

    /// <summary>
    /// Gets or sets the right child node. Automatically manages the <see cref="Parent"/> association.
    /// </summary>
    /// <value>
    /// The right child node, or <see langword="null"/> if there is no right child.
    /// </value>
    public virtual BinaryTreeNode<T>? Right
    {
        get => this.right;

        // Property setter should be used for automatic management of parent relationship.
        protected set
        {
            if (this.right == value)
            {
                return;
            }

            if (this.right is not null)
            {
                this.right.parent = null;
            }

            this.right = value;

            if (value is not null)
            {
                value.parent = this;
            }
        }
    }

    /// <summary>
    /// Gets the parent of this node.
    /// </summary>
    /// <value>
    /// The parent node, or <see langword="null"/> if this node is not in a tree or if it is the root node.
    /// </value>
    public virtual BinaryTreeNode<T>? Parent => this.parent;

    /// <summary>
    /// Gets the sibling of this node if it exists.
    /// </summary>
    /// <value>
    /// The sibling node, or <see langword="null"/> if there is no sibling.
    /// </value>
    public virtual BinaryTreeNode<T>? Sibling => this.Parent is null ? null : this.Parent.Left == this ? this.Parent.Right : this.Parent.Left;

    /// <summary>
    /// Gets a value indicating whether the node is a leaf node (has no children).
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the node is a leaf node; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsLeaf => this.Left is null && this.Right is null;

    /// <summary>
    /// Removes a child node from the current node.
    /// </summary>
    /// <param name="node">The child node to remove.</param>
    /// <exception cref="ArgumentException">If the node to be removed is not a child of the current node.</exception>
    public virtual void RemoveChild(BinaryTreeNode<T> node)
    {
        if (this.Left == node)
        {
            this.Left = null;
        }
        else
        {
            this.Right = this.Right == node
                ? null
                : throw new ArgumentException($"node to be removed `{node}` is not a child of `{this}`", nameof(node));
        }
    }

    /// <inheritdoc />
    public override string ToString()
    {
        string[] children =
        [
            this.Left is null ? string.Empty : $"{this.Left.Value}",
            this.Right is null ? string.Empty : $"{this.Right.Value}",
        ];

        var childrenStr = !string.IsNullOrEmpty(children[0]) || !string.IsNullOrEmpty(children[1])
            ? $" {{{string.Join(',', children)}}}"
            : string.Empty;
        return $"{this.Value}{childrenStr}";
    }

    /// <summary>
    /// Dumps the contents of a docking tree, starting at this node, to the Debug output.
    /// </summary>
    /// <param name="indentChar">The character used to indent children relative to their parent. Default is <c>' '</c>.</param>
    /// <param name="indentSize">The number of indent characters to use per indentation level. Default is <c>3</c>.</param>
    /// <param name="initialIndentLevel">Can be used to specify an initial indentation for the dumped info.</param>
    /// <remarks>
    /// This method recursively dumps the contents of the tree to the Debug output, starting from this node.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var rootNode = new BinaryTreeNode<int>(1);
    /// rootNode.Left = new BinaryTreeNode<int>(2);
    /// rootNode.Right = new BinaryTreeNode<int>(3);
    /// rootNode.Dump();
    /// ]]></code>
    /// </para>
    /// </remarks>
    public void Dump(
        char indentChar = ' ',
        int indentSize = 3,
        int initialIndentLevel = 0)
        => DumpRecursive(this, initialIndentLevel, indentChar, indentSize);

    /// <inheritdoc />
    /// <remarks>
    /// Disposes of the left and right child if they are not <see langword="null" />.
    /// Does <b>not</b> dispose of the stored value
    /// in <see cref="Value" />.
    /// </remarks>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        if (this.Value is IDisposable resource)
        {
            resource.Dispose();
        }

        this.Left?.Dispose();
        this.Right?.Dispose();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Swaps the left and right child nodes.
    /// </summary>
    /// <remarks>
    /// This method swaps the left and right child nodes without using the property setters to avoid messing up the parent association.
    /// </remarks>
    protected void SwapLeftAndRight() =>
        /*
         * Swap the left and right child. DO NOT use property setter here
         * because it will mess up the Parent association.
         */
        (this.left, this.right) = (this.right, this.left);

    private static void DumpRecursive(BinaryTreeNode<T>? node, int indentLevel, char indentChar, int indentSize)
    {
        if (node is null)
        {
            return;
        }

        var indent = new string(indentChar, indentLevel * indentSize); // 2 spaces per indent level
        Debug.WriteLine($"{indent}{node.Value}");
        DumpRecursive(node.Left, indentLevel + 1, indentChar, indentSize);
        DumpRecursive(node.Right, indentLevel + 1, indentChar, indentSize);
    }
}
