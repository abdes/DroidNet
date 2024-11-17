// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Detail;

namespace DroidNet.Docking;

/// <summary>
/// Represents a specialized <see cref="Dock"/> intended for docking the central view of the application,
/// typically hosting the main document(s).
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="CenterDock"/> is a unique type of dock that cannot be closed or minimized. It is
/// designed to be the central docking area of an application, ensuring that the main content
/// remains always visible and accessible.
/// </para>
/// <para>
/// This dock is created using the <see cref="New"/> method, which ensures that it is properly
/// instantiated and managed by the internal factory.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of <see cref="CenterDock"/>, use the following code:
/// </para>
/// <code><![CDATA[
/// var centerDock = CenterDock.New();
/// ]]></code>
/// </example>
public partial class CenterDock : Dock
{
    /// <inheritdoc/>
    public override bool CanMinimize => false;

    /// <inheritdoc/>
    public override bool CanClose => false;

    /// <summary>
    /// Creates a new instance of the <see cref="CenterDock"/> class.
    /// </summary>
    /// <returns>A new instance of <see cref="CenterDock"/>.</returns>
    /// <remarks>
    /// <para>
    /// This method uses the internal <see cref="Dock.Factory"/> to create an instance of
    /// <see cref="CenterDock"/>. It ensures that the dock is properly managed and assigned a unique ID.
    /// </para>
    /// <para>
    /// <strong>Note:</strong> The <see cref="CenterDock"/> cannot be closed or minimized. Attempting
    /// to do so will have no effect.
    /// </para>
    /// </remarks>
    /// <example>
    /// <para>
    /// To create a new <see cref="CenterDock"/>, use the following code:
    /// </para>
    /// <code><![CDATA[
    /// var centerDock = CenterDock.New();
    /// ]]></code>
    /// </example>
    public static CenterDock New() => (CenterDock)Factory.CreateDock(typeof(CenterDock));
}
