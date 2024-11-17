// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents an edge group within the docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="EdgeGroup"/> class is a specialized type of <see cref="LayoutGroup"/> that is used to manage edge segments within the docking workspace.
/// The orientation of an <see cref="EdgeGroup"/> can only be set at creation and cannot be changed afterwards.
/// </remarks>
/// <param name="docker">The docker that manages this edge group.</param>
/// <param name="orientation">The orientation of the edge group.</param>
/// <example>
/// <para>
/// <strong>Example Usage:</strong>
/// <code><![CDATA[
/// IDocker docker = ...;
/// var edgeGroup = new EdgeGroup(docker, DockGroupOrientation.Vertical);
/// ]]></code>
/// </para>
/// </example>
internal sealed class EdgeGroup(IDocker docker, DockGroupOrientation orientation) : LayoutGroup(docker, orientation)
{
    /// <inheritdoc/>
    /// <exception cref="InvalidOperationException">Thrown when attempting to set the orientation after creation.</exception>
    /// <remarks>
    /// The orientation of an <see cref="EdgeGroup"/> can only be set at creation and cannot be changed afterwards.
    /// </remarks>
    public override DockGroupOrientation Orientation
    {
        get => base.Orientation;
        internal set => throw new InvalidOperationException(
            $"orientation of an {nameof(EdgeGroup)} can only be set at creation");
    }
}
