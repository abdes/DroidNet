// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// The ViewModel for a docking tray control.
/// </summary>
public partial class DockTrayViewModel : ObservableObject
{
    private readonly ObservableCollection<IDockable> dockables = [];

    private readonly ReadOnlyObservableCollection<IDock> docks;
    private readonly IDocker docker;

    public DockTrayViewModel(IDockTray tray, Orientation orientation)
    {
        this.Dockables = new ReadOnlyObservableCollection<IDockable>(this.dockables);

        this.Orientation = orientation;
        this.docker = tray.Docker;
        this.docks = tray.Docks;

        // The tray is recreated every time it is needed. Therefore, we only
        // update the minimized dockables here. We do not need to track changes
        // to the docks and their dockables. A change will trigger a layout
        // update, which will recreate the trays as needed.
        this.UpdateDockables();
    }

    public Orientation Orientation { get; set; }

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    [RelayCommand]
    private void ShowDockable(IDockable dockable)
    {
        var dock = dockable.Owner;
        Debug.Assert(dock != null, "a minimized dock in the tray should always have a valid owner dock");

        this.docker.PinDock(dock);
    }

    private void UpdateDockables()
    {
        // Create a new list of IDockable objects in the order they appear in the docks collection
        var orderedDockables = this.docks.SelectMany(dock => dock.Dockables).ToList();

        // Update the dockables collection to match the ordered list
        for (var i = 0; i < orderedDockables.Count; i++)
        {
            if (this.dockables.Count > i)
            {
                if (!this.dockables[i].Equals(orderedDockables[i]))
                {
                    this.dockables.Insert(i, orderedDockables[i]);
                }
            }
            else
            {
                this.dockables.Add(orderedDockables[i]);
            }
        }

        // Remove any extra items from the dockables collection
        while (this.dockables.Count > orderedDockables.Count)
        {
            this.dockables.RemoveAt(this.dockables.Count - 1);
        }
    }
}
