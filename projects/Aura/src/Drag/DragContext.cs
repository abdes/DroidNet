// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Holds the contextual information about a drag operation, shared between the <see
///     cref="TabDragCoordinator"/> and the drag strategies during their lifecycle.
/// </summary>
/// <param name="tabStrip">The <see cref="ITabStrip"/> where the drag is currently happening.</param>
/// <param name="draggedItem">The item being dragged. Cannot be null.</param>
/// <param name="draggedItemIndex">The index of the dragged item in the TabStrip's Items collection.</param>
/// <param name="visualElement">The visual element for drag preview rendering.</param>
/// <param name="spatialMapper">
///     The spatialMapper spatialMapper, to be used when transforming coordinates from one space to another.
///     Created and maintained by the <see cref="TabDragCoordinator"/>, and kept in-sync to always
///     use the currently active <see cref="WindowSpace"/> and <see cref="ElementSpace"/> of the
///     drag operation.
/// </param>
public sealed class DragContext(
    ITabStrip? tabStrip,
    object draggedItem,
    int draggedItemIndex,
    Windows.Foundation.Point hotspotOffsets,
    FrameworkElement stripContainer,
    FrameworkElement draggedElement,
    ISpatialMapper spatialMapper)
{
    /// <summary>
    /// Gets or sets the TabStrip that initiated the drag operation.
    /// Null when cursor is in out-world space.
    /// </summary>
    public ITabStrip? TabStrip { get; set; } = tabStrip;

    public FrameworkElement TabStripContainer { get; } = stripContainer ?? throw new ArgumentNullException(nameof(stripContainer));

    /// <summary>
    /// Gets the dragged item. Required and immutable.
    /// </summary>
    public object DraggedItem { get; } = draggedItem ?? throw new ArgumentNullException(nameof(draggedItem));

    /// <summary>
    /// Gets the index of the dragged item in the TabStrip's Items collection.
    /// </summary>
    public int DraggedItemIndex { get; } = draggedItemIndex;

    public Point HotspotOffsets { get; } = hotspotOffsets;

    /// <summary>
    /// Gets the visual element for drag preview rendering (e.g., TabStripItem).
    /// </summary>
    public FrameworkElement DraggedVisualElement { get; } = draggedElement ?? throw new ArgumentNullException(nameof(draggedElement));

    /// <summary>
    /// Gets the spatial mapper. Required and immutable.
    /// </summary>
    public ISpatialMapper SpatialMapper { get; } = spatialMapper ?? throw new ArgumentNullException(nameof(spatialMapper));
}
