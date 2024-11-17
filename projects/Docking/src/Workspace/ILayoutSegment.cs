// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a segment in the layout of a docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="ILayoutSegment"/> interface defines the properties that describe the orientation, stretching behavior,
/// and the associated docker of a layout segment. Implementing this interface allows a segment to be managed within the docking layout.
/// </remarks>
public interface ILayoutSegment
{
    /// <summary>
    /// Gets the orientation of the layout segment.
    /// </summary>
    /// <value>
    /// A <see cref="DockGroupOrientation"/> value representing the orientation of the layout segment.
    /// </value>
    /// <remarks>
    /// The orientation determines how the segment is arranged within the layout. It can be horizontal, vertical, or undetermined.
    /// </remarks>
    public DockGroupOrientation Orientation { get; }

    /// <summary>
    /// Gets a value indicating whether the layout segment should stretch to fill the available space.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the segment should stretch to fill the available space; otherwise, <see langword="false"/>.
    /// </value>
    /// <remarks>
    /// When <see langword="true"/>, the segment will expand to occupy any remaining space in the layout. This is useful for ensuring
    /// that the layout is fully utilized and that there are no gaps.
    /// </remarks>
    public bool StretchToFill { get; }

    /// <summary>
    /// Gets the docker that manages this layout segment.
    /// </summary>
    /// <value>
    /// An <see cref="IDocker"/> instance representing the docker that manages this layout segment.
    /// </value>
    /// <remarks>
    /// The docker is responsible for managing the docking operations and layout of the segment within the workspace.
    /// </remarks>
    public IDocker Docker { get; }
}
