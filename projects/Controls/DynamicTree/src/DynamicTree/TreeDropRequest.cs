// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>An immutable, identity-based request for a tree drop operation.</summary>
public sealed class TreeDropRequest
{
    /// <summary>Initializes a new instance of the <see cref="TreeDropRequest"/> class.</summary>
    /// <param name="items">The dragged roots in their original order.</param>
    /// <param name="parent">The resolved destination parent.</param>
    /// <param name="index">The insertion index in the destination's child collection.</param>
    /// <param name="operation">The requested operation.</param>
    public TreeDropRequest(IReadOnlyList<ITreeItem> items, ITreeItem parent, int index, TreeDropOperation operation)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(parent);
        ArgumentOutOfRangeException.ThrowIfNegative(index);

        this.Items = Array.AsReadOnly(items.ToArray());
        this.Parent = parent;
        this.Index = index;
        this.Operation = operation;
    }

    /// <summary>Gets the dragged source roots.</summary>
    public IReadOnlyList<ITreeItem> Items { get; }

    /// <summary>Gets the destination parent.</summary>
    public ITreeItem Parent { get; }

    /// <summary>Gets the destination child insertion index.</summary>
    public int Index { get; }

    /// <summary>Gets the requested move/copy operation.</summary>
    public TreeDropOperation Operation { get; }
}
