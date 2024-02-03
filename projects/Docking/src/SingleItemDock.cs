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

    public static SingleItemDock? New() => Factory.CreateDock(typeof(SingleItemDock)) as SingleItemDock;
}
