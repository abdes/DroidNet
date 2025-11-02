// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents the context information passed to drag strategies during their lifecycle.
///     Contains references to the drag participants and state needed for strategy execution.
/// </summary>
public sealed class DragContext
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DragContext"/> class.
    /// </summary>
    /// <param name="draggedItem">The logical TabItem being dragged.</param>
    /// <param name="sourceStrip">The source TabStrip that initiated the drag.</param>
    /// <param name="sourceVisualItem">The visual TabStripItem container being dragged (may be null for TearOut mode).</param>
    /// <param name="hotspot">The logical hotspot offset for overlay alignment.</param>
    public DragContext(TabItem draggedItem, TabStrip sourceStrip, TabStripItem? sourceVisualItem, SpatialPoint hotspot)
    {
        this.DraggedItem = draggedItem ?? throw new ArgumentNullException(nameof(draggedItem));
        this.SourceStrip = sourceStrip ?? throw new ArgumentNullException(nameof(sourceStrip));
        this.SourceVisualItem = sourceVisualItem;
        this.Hotspot = hotspot;
    }

    /// <summary>
    ///     Gets the logical TabItem being dragged.
    /// </summary>
    public TabItem DraggedItem { get; }

    /// <summary>
    ///     Gets the source TabStrip that initiated the drag operation.
    /// </summary>
    public TabStrip SourceStrip { get; }

    /// <summary>
    ///     Gets the visual TabStripItem container being dragged.
    ///     This may be null during TearOut mode when the item has been removed from the visual tree.
    /// </summary>
    public TabStripItem? SourceVisualItem { get; }

    /// <summary>
    ///     Gets the logical hotspot offset for overlay alignment.
    ///     This value is passed to the drag visual service to ensure correct cursor-to-overlay alignment.
    /// </summary>
    public SpatialPoint Hotspot { get; }
}
