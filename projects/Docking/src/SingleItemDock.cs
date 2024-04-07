// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

/// <summary>
/// Represents a dock that holds only one dockable. Any attempt to add a
/// <see cref="IDockable" /> to it will result in replacing the current one.
/// </summary>
public class SingleItemDock : Dock
{
    protected SingleItemDock()
    {
    }

    public static SingleItemDock New() => (SingleItemDock)Factory.CreateDock(typeof(SingleItemDock));

    public override void AdoptDockable(IDockable dockable, DockablePlacement position = DockablePlacement.Last)
    {
        if (this.Dockables.Count != 0)
        {
            throw new InvalidOperationException(
                $"attempt to add a dockable to a single item dock which already has one ({this.Dockables[0].ViewModel})");
        }

        base.AdoptDockable(dockable, position);
    }
}
