// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Utils;
using DroidNet.Docking.Workspace;

/// <summary>
/// Represents an abstract base class for layout flow, which defines the flow direction
/// and description of a layout segment.
/// </summary>
/// <param name="segment">The layout segment associated with this layout flow.</param>
public abstract class LayoutFlow(ILayoutSegment segment)
{
    /// <summary>
    /// Gets the description of the layout flow, primarily used for debugging.
    /// </summary>
    public string Description { get; init; } = string.Empty;

    /// <summary>
    /// Gets the flow direction of the layout segment.
    /// </summary>
    public FlowDirection Direction { get; } = segment.Orientation.ToFlowDirection();

    /// <summary>
    /// Gets a value indicating whether the flow direction is horizontal (left to right).
    /// </summary>
    public bool IsHorizontal => this.Direction == FlowDirection.LeftToRight;

    /// <summary>
    /// Gets a value indicating whether the flow direction is vertical (top to bottom).
    /// </summary>
    public bool IsVertical => this.Direction == FlowDirection.TopToBottom;

    /// <inheritdoc />
    public override string ToString() => this.Description;
}
