// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Controls;

using DroidNet.Docking.Detail;

public class DockableInfoViewModel(IDockable dockable)
{
    public string DockableId => dockable.Id;

    public string DockId => dockable.Owner?.Id.ToString() ?? string.Empty;

    public string GroupInfo
    {
        get
        {
            var owner = dockable.Owner as Dock;
            return owner?.GroupInfo ?? string.Empty;
        }
    }
}
