// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Detail;

namespace DroidNet.Docking;

/// <summary>
/// Represents a dock that is most suitable for non-document dockable views, such as tool windows and auxiliary panels.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="ToolDock"/> is designed to host tool windows and other non-document views within an application. It provides a flexible
/// and organized way to manage auxiliary panels that complement the main content.
/// </para>
/// <para>
/// This dock can contain multiple dockable items, making it ideal for scenarios where various tools and utilities need to be accessible
/// alongside the main application content.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of <see cref="ToolDock"/>, use the following code:
/// </para>
/// <code><![CDATA[
/// var toolDock = ToolDock.New();
/// ]]></code>
/// </example>
public partial class ToolDock : Dock
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ToolDock"/> class.
    /// </summary>
    protected ToolDock()
    {
    }

    /// <summary>
    /// Creates a new instance of the <see cref="ToolDock"/> class.
    /// </summary>
    /// <returns>A new instance of <see cref="ToolDock"/>.</returns>
    /// <remarks>
    /// <para>
    /// This method uses the internal <see cref="Dock.Factory"/> to create an instance of <see cref="ToolDock"/>. It ensures that the dock is properly
    /// managed and assigned a unique ID.
    /// </para>
    /// </remarks>
    /// <example>
    /// <para>
    /// To create a new <see cref="ToolDock"/>, use the following code:
    /// </para>
    /// <code><![CDATA[
    /// var toolDock = ToolDock.New();
    /// ]]></code>
    /// </example>
    public static ToolDock New() => (ToolDock)Factory.CreateDock(typeof(ToolDock));
}
