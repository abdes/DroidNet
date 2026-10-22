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
/// This dock is created by <see cref="IDocker.CreateDock{TDock}"/>, which gives it an ID unique in that docker.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of <see cref="CenterDock"/>, use the following code:
/// </para>
/// <code><![CDATA[
/// var centerDock = docker.CreateDock<CenterDock>();
/// ]]></code>
/// </example>
public partial class CenterDock : Dock
{
    /// <inheritdoc/>
    public override bool CanMinimize => false;

    /// <inheritdoc/>
    public override bool CanClose => false;
}
