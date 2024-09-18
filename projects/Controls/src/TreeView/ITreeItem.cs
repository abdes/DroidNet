// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.TreeView;

public interface ITreeItem
{
    string Label { get; }

    bool IsExpanded { get; set; }

    bool HasChildren { get; }

    bool IsRoot { get; }

    IEnumerable<ITreeItem> Children { get; }
}

/// <summary>
/// A <see cref="ITreeItem" /> that holds a reference to an application specific object of type <typeparamref name="T" /> or any
/// type derived from it.
/// </summary>
/// <typeparam name="T">
/// The type of the object that can be attached to this <see cref="ITreeItem{T}" />.
/// <remarks>
/// The out keyword specifies that the type parameter T is covariant. Covariance allows us to use a more derived type than
/// originally specified by the generic parameter. This is particularly useful in scenarios where we want to ensure that a type
/// can be safely substituted with its derived types, making the interface more flexible and allows greater reuse of code with
/// different types.
/// </remarks>
/// </typeparam>
/// <remarks>
/// This is a type-safe specialization of the generic <see cref="ITreeItem" /> interface, for use by implementations of custom
/// <see cref="TreeItemAdapter" /> classes. The <see cref="DynamicTreeControl" /> does not care about the type of object held by
/// the tree item, and it only manipulates it as a generic <see cref="ITreeItem" />.
/// </remarks>
public interface ITreeItem<out T> : ITreeItem
{
    public T Item { get; }
}
