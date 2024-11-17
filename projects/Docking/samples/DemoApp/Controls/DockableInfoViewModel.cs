// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Demo.Controls;

/// <summary>
/// Provides a view model for displaying information about a dockable entity.
/// </summary>
/// <param name="dockable">The dockable entity to provide information about.</param>
/// <remarks>
/// <para>
/// The <see cref="DockableInfoViewModel"/> class is designed to expose properties that provide information about a specific
/// <see cref="IDockable"/> instance, such as its ID, the ID of its owner dock, and group information.
/// </para>
/// <para>
/// This view model is useful for displaying detailed information about dockable entities in a user interface, such as in a
/// debugging or monitoring tool.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create an instance of <see cref="DockableInfoViewModel"/> and bind it to a view, use the following code:
/// </para>
/// <code><![CDATA[
/// var dockable = new CustomDockable();
/// var viewModel = new DockableInfoViewModel(dockable);
/// ]]></code>
/// </example>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public because the 'ViewModel' property in the View is public")]
public class DockableInfoViewModel(IDockable dockable)
{
    private readonly IDockable dockable = dockable;

    /// <summary>
    /// Gets the unique identifier of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the unique identifier of the dockable entity.
    /// </value>
    public string DockableId => this.dockable.Id;

    /// <summary>
    /// Gets the unique identifier of the dock that owns the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the unique identifier of the owner dock, or an empty string if the dockable is not owned by any dock.
    /// </value>
    public string DockId => this.dockable.Owner?.Id.ToString() ?? string.Empty;

    /// <summary>
    /// Gets the group information of the dock that owns the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the group information of the owner dock, or an empty string if the dockable is not owned by any dock or if the group information is not available.
    /// </value>
    public string GroupInfo
    {
        get
        {
            var owner = this.dockable.Owner as Dock;
            return owner?.GroupInfo ?? string.Empty;
        }
    }
}
