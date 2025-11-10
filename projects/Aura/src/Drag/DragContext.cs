// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Context shared between the <see cref="TabDragCoordinator"/> and drag strategies
///     during a drag operation. Contains both the immutable origin of the drag and the
///     mutable state that changes as the drag moves between tab strips or leaves them.
/// </summary>
/// <remarks>
///     <see cref="SourceTabStrip"/> records where the drag started. <see cref="TabStrip"/>
///     is the current host and is updated by the coordinator; it may be <see langword="null"/>
///     while the item is torn out. UI-affine operations (visual capture, preview requests,
///     control updates) must run on the UI thread.
/// </remarks>
public sealed class DragContext(
    ITabStrip? tabStrip,
    object draggedItemData,
    int draggedItemIndex,
    Point hotspotOffsets,
    FrameworkElement stripContainer,
    FrameworkElement draggedElement,
    ISpatialMapper spatialMapper)
{
    /// <summary>
    ///     Gets the tab strip where the drag originated. Set once in the constructor and not modified.
    ///     Use as a fallback when <see cref="TabStrip"/> is unavailable (for example during tear-out).
    /// </summary>
    public ITabStrip? SourceTabStrip { get; } = tabStrip;

    /// <summary>
    ///     Gets the current host of the dragged item. Updated by <see cref="UpdateCurrentStrip"/>.
    ///     May be <see langword="null"/> while the item is not hosted by any strip.
    /// </summary>
    public ITabStrip? TabStrip { get; private set; } = tabStrip;

    /// <summary>
    ///     Gets the container element of the active TabStrip, or <see langword="null"/> when
    ///     the item is not hosted by a strip.
    /// </summary>
    public FrameworkElement? TabStripContainer { get; private set; } = stripContainer ?? throw new ArgumentNullException(nameof(stripContainer));

    /// <summary>Gets the opaque dragged item data.</summary>
    public object DraggedItemData { get; } = draggedItemData ?? throw new ArgumentNullException(nameof(draggedItemData));

    /// <summary>Gets the index of the dragged item in the current TabStrip's Items collection.</summary>
    public int DraggedItemIndex { get; private set; } = draggedItemIndex;

    /// <summary>Gets the visual element used for drag preview rendering (for example, a TabStripItem).</summary>
    public FrameworkElement DraggedVisualElement { get; private set; } = draggedElement ?? throw new ArgumentNullException(nameof(draggedElement));

    /// <summary>Gets the offset from the top-left corner of the dragged element to the drag hotspot.</summary>
    public Point HotspotOffsets { get; } = hotspotOffsets;

    /// <summary>Gets the mapper used for coordinate conversions during the drag operation.</summary>
    public ISpatialMapper SpatialMapper { get; private set; } = spatialMapper ?? throw new ArgumentNullException(nameof(spatialMapper));

    /// <summary>
    ///     Updates the current host, its container, the spatial mapper and the logical
    ///     index of the dragged item. Intended to be called by the <see cref="TabDragCoordinator"/>.
    ///     Ensure UI-thread affinity when passing UI-bound mappers or visual containers.
    /// </summary>
    /// <param name="tabStrip">The strip that currently hosts the dragged item, or <see langword="null"/>.</param>
    /// <param name="stripContainer">The container element of the active strip, if any.</param>
    /// <param name="spatialMapper">The mapper that converts coordinates for the active strip.</param>
    /// <param name="draggedItemIndex">The logical index of the dragged item within the active strip.</param>
    public void UpdateCurrentStrip(ITabStrip? tabStrip, FrameworkElement? stripContainer, ISpatialMapper spatialMapper, int draggedItemIndex)
    {
        ArgumentNullException.ThrowIfNull(spatialMapper);

        this.TabStrip = tabStrip;
        this.TabStripContainer = stripContainer;
        this.SpatialMapper = spatialMapper;
        this.DraggedItemIndex = draggedItemIndex;
    }

    /// <summary>Updates the logical index of the dragged item within the active strip.</summary>
    /// <param name="draggedItemIndex">The new logical index.</param>
    public void UpdateDraggedItemIndex(int draggedItemIndex)
    {
        this.DraggedItemIndex = draggedItemIndex;
    }

    /// <summary>Updates the visual element associated with the dragged item.</summary>
    /// <param name="draggedElement">The new visual element.</param>
    public void UpdateDraggedVisualElement(FrameworkElement draggedElement)
    {
        ArgumentNullException.ThrowIfNull(draggedElement);

        this.DraggedVisualElement = draggedElement;
    }
}
