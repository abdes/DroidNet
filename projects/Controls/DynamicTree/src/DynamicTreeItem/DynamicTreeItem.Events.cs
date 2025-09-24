// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents an item within a dynamic tree structure, supporting on-demand loading of child items,
///     expansion and collapse, selection handling, in-place renaming, and hierarchical indentation.
/// </summary>
public partial class DynamicTreeItem
{
    /// <summary>
    ///     Fires when the contents under the <see cref="DynamicTreeItem" /> need to be expanded.
    /// </summary>
    /// <remarks>
    ///     This event should be handled by the containing tree to expand the content under this
    ///     <see cref="DynamicTreeItem" />.
    /// </remarks>
    public event EventHandler<DynamicTreeEventArgs>? Expand;

    /// <summary>
    ///     Fires when the contents under the <see cref="DynamicTreeItem" /> need to be collapsed.
    /// </summary>
    /// <remarks>
    ///     This event should be handled by the containing tree to collapse the content under this
    ///     <see cref="DynamicTreeItem" />.
    /// </remarks>
    public event EventHandler<DynamicTreeEventArgs>? Collapse;
}
