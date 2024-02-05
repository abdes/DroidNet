// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDock
{
    public DockId Id { get; }

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    DockingState State { get; }

    bool CanMinimize { get; }

    bool CanClose { get; }

    void AddDockable(IDockable dockable);
}
