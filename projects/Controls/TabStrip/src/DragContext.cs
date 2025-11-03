// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;

namespace DroidNet.Controls;

/// <summary>
///     Holds the contextual information about a drag operation, shared between the <see
///     cref="TabDragCoordinator"/> and the drag strategies during their lifecycle.
/// </summary>
/// <param name="tabStrip">The <see cref="Controls.TabStrip"/>, in which the drag is currently happening.</param>
/// <param name="draggedItem">The item being dragged. Cannot be null.</param>
/// <param name="spatialMapper">
///     The spatialMapper spatialMapper, to be used when transforming coordinates from one space to another.
///     Created and maintained by the <see cref="TabDragCoordinator"/>, and kept in-sync to always
///     use the currently active <see cref="WindowSpace"/> and <see cref="ElementSpace"/> of the
///     drag operation.
/// </param>
public sealed class DragContext(TabStrip tabStrip, TabItem draggedItem, ISpatialMapper spatialMapper)
{
    /// <summary>
    /// Gets or sets the TabStrip that initiated the drag operation.
    /// Must be non-null at initialization, but can be set to null later.
    /// </summary>
    public TabStrip? TabStrip { get; set; } = tabStrip ?? throw new ArgumentNullException(nameof(tabStrip));

    /// <summary>
    /// Gets the dragged item. Required and immutable.
    /// </summary>
    public TabItem DraggedItem { get; } = draggedItem ?? throw new ArgumentNullException(nameof(draggedItem));

    /// <summary>
    /// Gets the spatial mapper. Required and immutable.
    /// </summary>
    public ISpatialMapper SpatialMapper { get; } = spatialMapper ?? throw new ArgumentNullException(nameof(spatialMapper));
}
