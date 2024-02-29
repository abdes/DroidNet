// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Docking;
using Windows.Foundation;

/// <summary>The ViewModel for a dock panel.</summary>
/// <param name="dock">The dock.</param>
/// <param name="docker">The docker which can be used to manage the dock.</param>
public partial class DockPanelViewModel(IDock dock, IDocker docker) : ObservableObject
{
    [ObservableProperty]
    private string title = dock.ToString() ?? "EMPTY";

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }
        = new(new ObservableCollection<IDockable>(dock.Dockables));

    public void OnSizeChanged(Size newSize)
    {
        Debug.WriteLine($"DockPanel {dock.Id} size changed: {newSize}");

        docker.ResizeDock(dock, new IDockable.Width(newSize.Width), new IDockable.Height(newSize.Height));
    }

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
    private void AddDockable(Dockable dockable) => dock.AddDockable(dockable);

    private bool CanClose() => dock.CanClose;
}
