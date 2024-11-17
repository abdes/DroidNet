// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Detail;

namespace DroidNet.Docking;

/// <summary>
/// Represents a dock that holds only one dockable item. Any attempt to add a new <see cref="IDockable"/> will replace the existing one.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="SingleItemDock"/> is designed to manage a single dockable entity at a time. This is useful in scenarios where
/// only one main content area or tool window should be displayed within the dock.
/// </para>
/// <para>
/// When a new dockable is adopted, it replaces the current dockable if one already exists. This ensures that the dock always contains
/// exactly one dockable item.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of <see cref="SingleItemDock"/>, use the following code:
/// </para>
/// <code><![CDATA[
/// var singleItemDock = SingleItemDock.New();
/// ]]></code>
/// </example>
public partial class SingleItemDock : Dock
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SingleItemDock"/> class.
    /// </summary>
    protected SingleItemDock()
    {
    }

    /// <summary>
    /// Creates a new instance of the <see cref="SingleItemDock"/> class.
    /// </summary>
    /// <returns>A new instance of <see cref="SingleItemDock"/>.</returns>
    /// <remarks>
    /// <para>
    /// This method uses the internal <see cref="Dock.Factory"/> to create an instance of <see cref="SingleItemDock"/>. It ensures that the dock is properly
    /// managed and assigned a unique ID.
    /// </para>
    /// </remarks>
    /// <example>
    /// <para>
    /// To create a new <see cref="SingleItemDock"/>, use the following code:
    /// </para>
    /// <code><![CDATA[
    /// var singleItemDock = SingleItemDock.New();
    /// ]]></code>
    /// </example>
    public static SingleItemDock New() => (SingleItemDock)Factory.CreateDock(typeof(SingleItemDock));

    /// <summary>
    /// Adopts a dockable entity into the dock, replacing any existing dockable.
    /// </summary>
    /// <param name="dockable">The dockable entity to adopt.</param>
    /// <param name="position">The position at which to place the dockable entity. The default is <see cref="DockablePlacement.Last"/>.</param>
    /// <exception cref="InvalidOperationException">Thrown when attempting to add a dockable to a <see cref="SingleItemDock"/> that already contains one.</exception>
    /// <remarks>
    /// <para>
    /// If the dock already contains a dockable, this method will throw an <see cref="InvalidOperationException"/>. This ensures that the dock
    /// always contains exactly one dockable item.
    /// </para>
    /// </remarks>
    /// <example>
    /// <para>
    /// To adopt a dockable entity into a <see cref="SingleItemDock"/>, use the following code:
    /// </para>
    /// <code><![CDATA[
    /// var singleItemDock = SingleItemDock.New();
    /// var dockable = new CustomDockable();
    /// singleItemDock.AdoptDockable(dockable);
    /// ]]></code>
    /// </example>
    public override void AdoptDockable(IDockable dockable, DockablePlacement position = DockablePlacement.Last)
    {
        if (this.Dockables.Count != 0)
        {
            throw new InvalidOperationException(
                $"Attempt to add a dockable to a single item dock which already has one ({this.Dockables[0].ViewModel})");
        }

        base.AdoptDockable(dockable, position);
    }
}
