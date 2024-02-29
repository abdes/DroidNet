// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Collections.ObjectModel;

public interface IDock : IDisposable
{
    DockId Id { get; }

    ReadOnlyCollection<IDockable> Dockables { get; }

    IDockable? ActiveDockable { get; set; }

    DockingState State { get; }

    bool CanMinimize { get; }

    bool CanClose { get; }

    Anchor? Anchor { get; }

    Width Width { get; set; }

    Height Height { get; set; }

    void AddDockable(Dockable dockable, DockablePlacement position = DockablePlacement.First);
}
