// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;

internal class DockGroupWithTray : DockGroup, IDockTray
{
    private readonly ObservableCollection<IDock> minimizedDocks = [];

    public DockGroupWithTray() => this.MinimizedDocks = new ReadOnlyObservableCollection<IDock>(this.minimizedDocks);

    public ReadOnlyObservableCollection<IDock> MinimizedDocks { get; }

    public bool HasMinimizedDocks => this.minimizedDocks.Count != 0;

    internal void AddMinimizedDock(IDock dock) => this.minimizedDocks.Add(dock);

    internal bool RemoveMinimizedDock(IDock dock) => this.minimizedDocks.Remove(dock);
}
