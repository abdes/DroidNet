// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a layout group within the docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="LayoutGroup"/> class is used to group layout segments within the docking workspace. It inherits from <see cref="LayoutSegment"/> and provides additional functionality specific to layout groups.
/// </remarks>
/// <param name="docker">The docker that manages this layout group.</param>
/// <param name="orientation">The orientation of the layout group. The default is <see cref="DockGroupOrientation.Undetermined"/>.</param>
/// <example>
/// <para>
/// <strong>Example Usage:</strong>
/// <code><![CDATA[
/// IDocker docker = ...;
/// var layoutGroup = new LayoutGroup(docker, DockGroupOrientation.Horizontal);
/// ]]></code>
/// </para>
/// </example>
internal class LayoutGroup(IDocker docker, DockGroupOrientation orientation = DockGroupOrientation.Undetermined)
    : LayoutSegment(docker, orientation)
{
    /// <summary>
    /// Returns a string that represents the current object.
    /// </summary>
    /// <returns>A string that represents the current object.</returns>
    /// <remarks>
    /// This method overrides the <see cref="object.ToString"/> method to provide a string representation of the layout group.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var layoutGroup = new LayoutGroup(docker, DockGroupOrientation.Horizontal);
    /// Console.WriteLine(layoutGroup.ToString());
    /// ]]></code>
    /// </para>
    /// </remarks>
    public override string ToString() => $"{base.ToString()}";
}
