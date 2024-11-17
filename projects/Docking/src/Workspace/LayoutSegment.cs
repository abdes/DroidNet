// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Utils;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a segment in the layout of a docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="LayoutSegment"/> class provides the base implementation for layout segments within a docking workspace. It defines properties for orientation, stretching behavior, and the associated docker.
/// </remarks>
/// <param name="docker">The docker that manages this layout segment.</param>
/// <param name="orientation">The orientation of the layout segment.</param>
public abstract class LayoutSegment(IDocker docker, DockGroupOrientation orientation) : ILayoutSegment
{
    private static int nextId = 1;

    /// <summary>
    /// Gets the orientation of the layout segment.
    /// </summary>
    /// <value>
    /// A <see cref="DockGroupOrientation"/> value representing the orientation of the layout segment.
    /// </value>
    /// <remarks>
    /// The orientation determines how the segment is arranged within the layout. It can be horizontal, vertical, or undetermined.
    /// </remarks>
    public virtual DockGroupOrientation Orientation { get; internal set; } = orientation;

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
    public virtual bool StretchToFill { get; internal set; }

    /// <summary>
    /// Gets the docker that manages this layout segment.
    /// </summary>
    /// <value>
    /// An <see cref="IDocker"/> instance representing the docker that manages this layout segment.
    /// </value>
    /// <remarks>
    /// The docker is responsible for managing the docking operations and layout of the segment within the workspace.
    /// </remarks>
    public IDocker Docker { get; } = docker;

    /// <summary>
    /// Gets the debug identifier for this layout segment.
    /// </summary>
    /// <value>
    /// An <see langword="int"/> representing the debug identifier for this layout segment.
    /// </value>
    /// <remarks>
    /// The debug identifier is useful for debugging purposes to uniquely identify layout segments.
    /// </remarks>
    public int DebugId { get; } = Interlocked.Increment(ref nextId);

    /// <summary>
    /// Returns a string that represents the current object.
    /// </summary>
    /// <returns>A string that represents the current object.</returns>
    /// <remarks>
    /// This method overrides the <see cref="object.ToString"/> method to provide a string representation of the layout segment.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var layoutSegment = new CustomLayoutSegment(docker, DockGroupOrientation.Horizontal);
    /// Console.WriteLine(layoutSegment.ToString());
    /// ]]></code>
    /// </para>
    /// </remarks>
    public override string? ToString()
        => $"{this.Orientation.ToSymbol()}{(this.StretchToFill ? " *" : string.Empty)} {this.DebugId}";
}
