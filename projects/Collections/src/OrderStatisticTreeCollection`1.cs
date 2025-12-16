// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Diagnostics;

namespace DroidNet.Collections;

/// <summary>
/// A balanced order-statistic binary search tree (red-black) that supports
/// rank and select operations in O(log n) time.
/// </summary>
/// <typeparam name="T">Item type. Comparison is done using the provided comparer.</typeparam>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "multiple constructors")]
public class OrderStatisticTreeCollection<T> : IReadOnlyCollection<T>
    where T : notnull
{
    private readonly IComparer<T> comparer;
    private Node? root;

    /// <summary>
    /// Initializes a new instance of the <see cref="OrderStatisticTreeCollection{T}"/> class
    /// using the default comparer for <typeparamref name="T"/>.
    /// </summary>
    public OrderStatisticTreeCollection()
        : this(Comparer<T>.Default)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="OrderStatisticTreeCollection{T}"/> class
    /// with the provided <paramref name="comparer"/>.
    /// </summary>
    /// <param name="comparer">Comparer used to order elements; when null the default comparer is used.</param>
    public OrderStatisticTreeCollection(IComparer<T>? comparer)
    {
        this.comparer = comparer ?? Comparer<T>.Default;
    }

    /// <summary>
    /// Gets number of elements contained in the tree.
    /// </summary>
    public int Count => this.root?.SubtreeSize ?? 0;

    /// <summary>
    /// Gets the root node of the tree.
    /// </summary>
    protected Node? Root => this.root;

    /// <summary>
    /// Adds <paramref name="item"/> to the tree.
    /// Duplicates are allowed and are inserted to the right.
    /// </summary>
    /// <param name="item">Item to add.</param>
    public void Add(T item)
    {
        var inserted = this.CreateNode(item);
        if (this.root is null)
        {
            this.root = inserted;
            this.root.IsRed = false; // root is black
            this.OnNodeUpdated(this.root);
#if DEBUG
            this.AssertInvariants();
#endif
            return;
        }

        var x = this.root;
        Node? parent = null;
        var parentCmp = 0;
        while (x is not null)
        {
            parent = x;
            parentCmp = this.comparer.Compare(item, x.Value);
            if (parentCmp < 0)
            {
                x = x.Left;
            }
            else
            {
                x = x.Right;
            }
        }

        inserted.Parent = parent;
        if (parentCmp < 0)
        {
            parent!.Left = inserted;
        }
        else
        {
            parent!.Right = inserted;
        }

        this.UpdateSizeUpwards(inserted);
        this.InsertFixup(inserted);
    }

    /// <summary>
    /// Determines whether the tree contains <paramref name="item"/>.
    /// </summary>
    /// <param name="item">Item to locate in the tree.</param>
    /// <returns><see langword="true"/> if an equal item exists in the tree; otherwise <see langword="false"/>.</returns>
    public bool Contains(T item) => this.FindNode(item) is not null;

    /// <summary>
    /// Removes an element equal to <paramref name="item"/> if present.
    /// </summary>
    /// <param name="item">Item to remove.</param>
    /// <returns><see langword="true"/> if an item was removed; otherwise <see langword="false"/>.</returns>
    public bool Remove(T item)
    {
        var z = this.FindNode(item);
        if (z is null)
        {
            return false;
        }

        this.DeleteNode(z);
        return true;
    }

    /// <summary>
    /// Returns the number of elements strictly less than <paramref name="item"/>.
    /// </summary>
    /// <param name="item">Item whose rank to compute.</param>
    /// <returns>Number of elements less than <paramref name="item"/>.</returns>
    public int Rank(T item)
    {
        var rank = 0;
        var x = this.root;
        while (x is not null)
        {
            var cmp = this.comparer.Compare(item, x.Value);
            if (cmp <= 0)
            {
                x = x.Left;
            }
            else
            {
                rank += (x.Left?.SubtreeSize ?? 0) + 1;
                x = x.Right;
            }
        }

        return rank;
    }

    /// <summary>
    /// Returns the element with the given zero-based rank (in-order index).
    /// </summary>
    /// <param name="index">Zero-based in-order index to select.</param>
    /// <returns>The element at the specified index.</returns>
    /// <exception cref="ArgumentOutOfRangeException">If <paramref name="index"/> is out of range.</exception>
    public T Select(int index)
    {
        if (index < 0 || index >= this.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }

        var x = this.root!;
        while (true)
        {
            var leftSize = x.Left?.SubtreeSize ?? 0;
            if (index < leftSize)
            {
                x = x.Left!;
            }
            else
            {
                if (index == leftSize)
                {
                    return x.Value;
                }

                index -= leftSize + 1;
                x = x.Right!;
            }
        }
    }

    /// <summary>
    /// Returns an enumerator that iterates the tree in-order.
    /// </summary>
    /// <returns>An <see cref="IEnumerator{T}"/> that iterates the collection.</returns>
    public IEnumerator<T> GetEnumerator()
    {
        return InOrder(this.root).GetEnumerator();

        static IEnumerable<T> InOrder(Node? node)
        {
            if (node is null)
            {
                yield break;
            }

            foreach (var v in InOrder(node.Left))
            {
                yield return v;
            }

            yield return node.Value;
            foreach (var v in InOrder(node.Right))
            {
                yield return v;
            }
        }
    }

    /// <summary>
    /// Returns an enumerator that iterates through the collection.
    /// </summary>
    /// <returns>An <see cref="IEnumerator"/> that can be used to iterate through the collection.</returns>
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <summary>
    /// Creates a new node for the tree. Override this to return a custom node type.
    /// </summary>
    /// <param name="value">The value to store in the node.</param>
    /// <returns>A new <see cref="Node"/> instance.</returns>
    protected virtual Node CreateNode(T value) => new(value);

    /// <summary>
    /// Called when a node's structure or children have changed.
    /// Use this to update augmented data in the node.
    /// The default implementation only needs to handle custom data, as SubtreeSize is managed by the base class.
    /// </summary>
    /// <param name="node">The node that was updated.</param>
    protected virtual void OnNodeUpdated(Node node)
    {
    }

    private static Node Minimum(Node x)
    {
        while (x.Left is not null)
        {
            x = x.Left;
        }

        return x;
    }

    private void UpdateSizeUpwards(Node? x)
    {
        var depth = 0;
        while (x is not null)
        {
#if DEBUG
            // Safety: Detect cycles in parent pointers (e.g. from bugs in rotation/delete logic).
            // This prevents infinite hangs in production if the tree structure is corrupted.
            if (depth++ > 1000)
            {
                throw new InvalidOperationException("Possible cycle detected in parent pointers during size update.");
            }
#endif // DEBUG
            x.SubtreeSize = 1 + (x.Left?.SubtreeSize ?? 0) + (x.Right?.SubtreeSize ?? 0);
            this.OnNodeUpdated(x);
            x = x.Parent;
        }
    }

    private Node? FindNode(T item)
    {
        var x = this.root;
        while (x is not null)
        {
            var cmp = this.comparer.Compare(item, x.Value);
            if (cmp == 0)
            {
                return x;
            }

            if (cmp < 0)
            {
                x = x.Left;
            }
            else
            {
                x = x.Right;
            }
        }

        return null;
    }

    private void LeftRotate(Node x)
    {
        var y = x.Right!;
        x.Right = y.Left;
        _ = y.Left?.Parent = x;

        y.Parent = x.Parent;
        if (x.Parent is null)
        {
            this.root = y;
        }
        else if (x == x.Parent.Left)
        {
            x.Parent.Left = y;
        }
        else
        {
            x.Parent.Right = y;
        }

        y.Left = x;
        x.Parent = y;

        // update subtree sizes (x then y)
        x.SubtreeSize = 1 + (x.Left?.SubtreeSize ?? 0) + (x.Right?.SubtreeSize ?? 0);
        this.OnNodeUpdated(x);

        y.SubtreeSize = 1 + (y.Left?.SubtreeSize ?? 0) + (y.Right?.SubtreeSize ?? 0);
        this.OnNodeUpdated(y);
    }

    private void RightRotate(Node y)
    {
        var x = y.Left!;
        y.Left = x.Right;
        _ = x.Right?.Parent = y;

        x.Parent = y.Parent;
        if (y.Parent is null)
        {
            this.root = x;
        }
        else if (y == y.Parent.Left)
        {
            y.Parent.Left = x;
        }
        else
        {
            y.Parent.Right = x;
        }

        x.Right = y;
        y.Parent = x;

        // update subtree sizes (y then x)
        y.SubtreeSize = 1 + (y.Left?.SubtreeSize ?? 0) + (y.Right?.SubtreeSize ?? 0);
        this.OnNodeUpdated(y);

        x.SubtreeSize = 1 + (x.Left?.SubtreeSize ?? 0) + (x.Right?.SubtreeSize ?? 0);
        this.OnNodeUpdated(x);
    }

    private void InsertFixup(Node z)
    {
        while (z.Parent?.IsRed == true)
        {
            if (z.Parent == z.Parent.Parent!.Left)
            {
                var y = z.Parent.Parent.Right;
                if (y?.IsRed == true)
                {
                    z.Parent.IsRed = false;
                    y.IsRed = false;
                    z.Parent.Parent.IsRed = true;
                    z = z.Parent.Parent;
                }
                else
                {
                    if (z == z.Parent.Right)
                    {
                        z = z.Parent;
                        this.LeftRotate(z);
                    }

                    z.Parent!.IsRed = false;
                    z.Parent.Parent!.IsRed = true;
                    this.RightRotate(z.Parent.Parent);
                }
            }
            else
            {
                var y = z.Parent.Parent!.Left;
                if (y?.IsRed == true)
                {
                    z.Parent.IsRed = false;
                    y.IsRed = false;
                    z.Parent.Parent!.IsRed = true;
                    z = z.Parent.Parent!;
                }
                else
                {
                    if (z == z.Parent.Left)
                    {
                        z = z.Parent;
                        this.RightRotate(z);
                    }

                    z.Parent!.IsRed = false;
                    z.Parent.Parent!.IsRed = true;
                    this.LeftRotate(z.Parent.Parent!);
                }
            }
        }

        this.root!.IsRed = false;
#if DEBUG
        this.AssertInvariants();
#endif
    }

    private void Transplant(Node u, Node? v)
    {
        if (u.Parent is null)
        {
            this.root = v;
        }
        else if (u == u.Parent.Left)
        {
            u.Parent.Left = v;
        }
        else
        {
            u.Parent.Right = v;
        }

        _ = v?.Parent = u.Parent;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "keep the logic together for maintainability")]
    private void DeleteNode(Node z)
    {
        var y = z;
        var yOriginalRed = y.IsRed;
        Node? x;
        Node? xParent;

        if (z.Left is null)
        {
            x = z.Right;
            this.Transplant(z, z.Right);
            this.UpdateSizeUpwards(z.Parent);
            xParent = z.Parent;

            // FIX: Explicitly clear Parent pointer. In complex scenarios, 'z' is logically removed
            // but might still be pointed to by other "ghost" nodes or during rotation artifacts.
            // Failing to clear this can cause UpdateSizeUpwards to traverse back into 'z' and create an infinite loop.
            z.Parent = null;
        }
        else if (z.Right is null)
        {
            x = z.Left;
            this.Transplant(z, z.Left);
            this.UpdateSizeUpwards(z.Parent);
            xParent = z.Parent;
            z.Parent = null;
        }
        else
        {
            y = OrderStatisticTreeCollection<T>.Minimum(z.Right);
            yOriginalRed = y.IsRed;
            x = y.Right;
            if (y.Parent == z)
            {
                x?.Parent = y;

                xParent = y;
            }
            else
            {
                var yParentBeforeMove = y.Parent;
                this.Transplant(y, y.Right);
                y.Parent = null; // Detach y
                y.Right = z.Right;
                y.Right.Parent = y;
                this.UpdateSizeUpwards(yParentBeforeMove);

                xParent = yParentBeforeMove;
            }

            this.Transplant(z, y);
            y.Left = z.Left;
            y.Left.Parent = y;
            y.IsRed = z.IsRed;

            z.Parent = null; // Detach z completely
            z.Left = null;
            z.Right = null;

            // recalc size for y and upwards
            y.SubtreeSize = 1 + (y.Left?.SubtreeSize ?? 0) + (y.Right?.SubtreeSize ?? 0);
            this.OnNodeUpdated(y);
            this.UpdateSizeUpwards(y.Parent);
        }

        if (!yOriginalRed)
        {
            this.DeleteFixup(x, xParent);
        }

#if DEBUG
        this.AssertInvariants();
#endif
    }

    // x is the node that moved into y's original position (may be null)
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "method is more maintainable the way it is")]
    private void DeleteFixup(Node? x, Node? parent)
    {
        while ((x?.IsRed != true) && this.root != x)
        {
            if (parent is null)
            {
                break; // safety
            }

            if (x == parent.Left)
            {
                var w = parent.Right;
                if (w?.IsRed == true)
                {
                    w.IsRed = false;
                    parent.IsRed = true;
                    this.LeftRotate(parent);
                    w = parent.Right;
                }

                if ((w?.Left is null || !w.Left.IsRed) && (w?.Right is null || !w.Right.IsRed))
                {
                    _ = w?.IsRed = true;

                    x = parent;
                    parent = x.Parent;
                }
                else
                {
                    if (w?.Right is null || !w.Right.IsRed)
                    {
                        if (w?.Left is not null)
                        {
                            w.Left.IsRed = false;
                        }

                        _ = w?.IsRed = true;

                        this.RightRotate(w!);
                        w = parent.Right;
                    }

                    if (w is not null)
                    {
                        w.IsRed = parent.IsRed;
                        parent.IsRed = false;
                        _ = w.Right?.IsRed = false;

                        this.LeftRotate(parent);
                    }

                    x = this.root;
                    parent = x?.Parent;
                }
            }
            else
            {
                var w = parent.Left;
                if (w?.IsRed == true)
                {
                    w.IsRed = false;
                    parent.IsRed = true;
                    this.RightRotate(parent);
                    w = parent.Left;
                }

                if ((w?.Right is null || !w.Right.IsRed) && (w?.Left is null || !w.Left.IsRed))
                {
                    _ = w?.IsRed = true;

                    x = parent;
                    parent = x.Parent;
                }
                else
                {
                    if (w?.Left is null || !w.Left.IsRed)
                    {
                        if (w?.Right is not null)
                        {
                            w.Right.IsRed = false;
                        }

                        _ = w?.IsRed = true;

                        this.LeftRotate(w!);
                        w = parent.Left;
                    }

                    if (w is not null)
                    {
                        w.IsRed = parent.IsRed;
                        parent.IsRed = false;
                        _ = w.Left?.IsRed = false;

                        this.RightRotate(parent);
                    }

                    x = this.root;
                    parent = x?.Parent;
                }
            }
        }

        _ = x?.IsRed = false;
    }

#if DEBUG
    [Conditional("DEBUG")]
    private void AssertInvariants()
    {
        if (this.root is null)
        {
            if (this.Count != 0)
            {
                throw new InvalidOperationException("Tree is empty but Count is non-zero.");
            }

            return;
        }

        if (this.root.IsRed)
        {
            throw new InvalidOperationException("Root must be black.");
        }

        var blackHeight = this.ValidateNode(
            this.root,
            parent: null,
            hasMin: false,
            min: default!,
            minInclusive: true,
            hasMax: false,
            max: default!,
            maxInclusive: true);

        if (blackHeight <= 0)
        {
            throw new InvalidOperationException("Invalid black-height.");
        }

        if (this.root.SubtreeSize != this.Count)
        {
            throw new InvalidOperationException("Root subtree size does not match Count.");
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "debug only invariants validator")]
    private int ValidateNode(
        Node? node,
        Node? parent,
        bool hasMin,
        T min,
        bool minInclusive,
        bool hasMax,
        T max,
        bool maxInclusive)
    {
        if (node is null)
        {
            return 1; // null leaves are black
        }

        if (node.Parent != parent)
        {
            throw new InvalidOperationException("Parent pointer is inconsistent.");
        }

        if (hasMin)
        {
            var cmp = this.comparer.Compare(node.Value, min);
            if (cmp < 0 || (cmp == 0 && !minInclusive))
            {
                throw new InvalidOperationException("BST invariant violated (below minimum bound).");
            }
        }

        if (hasMax)
        {
            var cmp = this.comparer.Compare(node.Value, max);
            if (cmp > 0 || (cmp == 0 && !maxInclusive))
            {
                throw new InvalidOperationException("BST invariant violated (above maximum bound).");
            }
        }

        if (node.IsRed)
        {
            if (node.Left?.IsRed == true)
            {
                throw new InvalidOperationException("Red node has red left child.");
            }

            if (node.Right?.IsRed == true)
            {
                throw new InvalidOperationException("Red node has red right child.");
            }
        }

        var leftBlackHeight = this.ValidateNode(
            node.Left,
            parent: node,
            hasMin: hasMin,
            min: min,
            minInclusive: minInclusive,
            hasMax: true,
            max: node.Value,
            maxInclusive: true);

        var rightBlackHeight = this.ValidateNode(
            node.Right,
            parent: node,
            hasMin: true,
            min: node.Value,
            minInclusive: true,
            hasMax: hasMax,
            max: max,
            maxInclusive: maxInclusive);

        if (leftBlackHeight != rightBlackHeight)
        {
            throw new InvalidOperationException("Black-height differs between left and right subtrees.");
        }

        var expectedSize = 1 + (node.Left?.SubtreeSize ?? 0) + (node.Right?.SubtreeSize ?? 0);
        return node.SubtreeSize != expectedSize
            ? throw new InvalidOperationException("SubtreeSize is inconsistent.")
            : leftBlackHeight + (node.IsRed ? 0 : 1);
    }
#endif

    /// <summary>
    /// Represents a node in the red-black order-statistic tree.
    /// </summary>
    protected class Node
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="Node"/> class that holds the specified value.
        /// New nodes are created red and with a subtree size of 1.
        /// </summary>
        /// <param name="value">The value to store in the node.</param>
        public Node(T value)
        {
            this.Value = value;
            this.IsRed = true;
            this.SubtreeSize = 1;
        }

        /// <summary>
        /// Gets the value stored in the node.
        /// </summary>
        public T Value { get; private set; }

        /// <summary>
        /// Gets or sets the left child of this node. Null indicates there is no left child.
        /// </summary>
        public Node? Left { get; set; }

        /// <summary>
        /// Gets or sets the right child of this node. Null indicates there is no right child.
        /// </summary>
        public Node? Right { get; set; }

        /// <summary>
        /// Gets or sets the parent of this node, or null if this node is the root or detached.
        /// </summary>
        public Node? Parent { get; set; }

        /// <summary>
        /// Gets or sets a value indicating whether this node is red.
        /// <c>true</c> means the node is red; <c>false</c> means the node is black.
        /// </summary>
        public bool IsRed { get; set; }

        /// <summary>
        /// Gets or sets the size of the subtree rooted at this node, including the node itself.
        /// This value is maintained by the tree to support rank/select (order-statistic) operations.
        /// </summary>
        public int SubtreeSize { get; set; } // includes this node
    }
}
