// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking.Workspace;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Docking.Controls;

/// <summary>
/// The ViewModel for a docking tray control.
/// </summary>
/// <remarks>
/// The <see cref="DockTrayViewModel"/> class provides the data and commands necessary to manage the
/// dockable items within a tray. It supports operations such as showing dockables and updating the
/// list of dockables.
/// </remarks>
public partial class DockTrayViewModel : ObservableObject
{
    private readonly ObservableCollection<IDockable> dockables = [];

    private readonly ReadOnlyObservableCollection<IDock> docks;
    private readonly IDocker docker;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockTrayViewModel"/> class with the specified tray and orientation.
    /// </summary>
    /// <param name="tray">The tray group that this ViewModel manages.</param>
    /// <param name="orientation">The orientation of the tray.</param>
    /// <remarks>
    /// This constructor sets up the ViewModel with the specified tray and orientation, and initializes
    /// the list of dockables.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var trayGroup = new TrayGroup(docker, AnchorPosition.Left);
    /// var viewModel = new DockTrayViewModel(trayGroup, Orientation.Horizontal);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public DockTrayViewModel(TrayGroup tray, Orientation orientation)
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

    /// <summary>
    /// Gets the orientation of the tray.
    /// </summary>
    /// <value>
    /// The orientation of the tray, either horizontal or vertical.
    /// </value>
    public Orientation Orientation { get; }

    /// <summary>
    /// Gets the collection of dockables in the tray.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IDockable"/> items in the tray.
    /// </value>
    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    /// <summary>
    /// Command to show a dockable item.
    /// </summary>
    /// <param name="dockable">The dockable item to show.</param>
    /// <remarks>
    /// This command pins the specified dockable item, making it visible in the tray.
    /// </remarks>
    [RelayCommand]
    private void ShowDockable(IDockable dockable)
    {
        var dock = dockable.Owner;
        Debug.Assert(dock != null, "a minimized dock in the tray should always have a valid owner dock");

        this.docker.PinDock(dock);
    }

    /// <summary>
    /// Updates the list of dockables in the tray.
    /// </summary>
    /// <remarks>
    /// This method updates the internal collection of dockables to match the current state of the
    /// docks. It ensures that the dockables are in the correct order and removes any extra items.
    /// </remarks>
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
