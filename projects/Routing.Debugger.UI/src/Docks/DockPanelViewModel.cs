// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;

/// <summary>The ViewModel for a dock panel.</summary>
/// <param name="dock">The dock.</param>
/// <param name="docker">The docker which can be used to manage the dock.</param>
public partial class DockPanelViewModel(IDock dock, IDocker docker) : ObservableObject
{
    [ObservableProperty]
    private string title = dock.ToString() ?? "EMPTY";

    public ReadOnlyObservableCollection<IDockable> Dockables => dock.Dockables;

    // TODO: combine CanMinimize and CanClose into a IsLocked flag where the dock is locked in place as Pinned
    [RelayCommand]
    private void TogglePinned()
    {
        switch (dock.State)
        {
            case DockingState.Minimized:
            case DockingState.Floating:
                docker.PinDock(dock);
                break;

            case DockingState.Pinned:
                docker.MinimizeDock(dock);
                break;

            case DockingState.Undocked:
            default:
                throw new InvalidOperationException(
                    $"attempt to toggle pinned state of a dock while it is not in a valid state: {dock.State}");
        }
    }

    [RelayCommand(CanExecute = nameof(CanClose))]
    private void Close() => docker.CloseDock(dock);

    [RelayCommand]
    private void AddDockable(IDockable dockable) => dock.AddDockable(dockable);

    private bool CanClose() => dock.CanClose;
}
