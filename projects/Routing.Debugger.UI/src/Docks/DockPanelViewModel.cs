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
    private bool initialSizeUpdate = true;
    private Size previousSize;

    [ObservableProperty]
    private string title = dock.ToString() ?? "EMPTY";

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }
        = new(new ObservableCollection<IDockable>(dock.Dockables));

    public void OnSizeChanged(Size newSize)
    {
        // Round the size to the nearest integer.
        var newWidth = double.Round(newSize.Width);
        var newHeight = double.Round(newSize.Height);

        // If this is the initial size update, we memorize it but we do not
        // trigger a resize of the dock. We only trigger the resize if the size
        // changes after the initial update.
        if (this.initialSizeUpdate)
        {
            Debug.WriteLine($"DockPanel {dock.Id} this is our initial size: Width={newWidth}, Height={newHeight}");
            this.previousSize.Width = newWidth;
            this.previousSize.Height = newHeight;
            this.initialSizeUpdate = false;
            return;
        }

        bool widthChanged = false, heightChanged = false;

        if (newWidth != this.previousSize.Width)
        {
            Debug.WriteLine($"DockPanel {dock.Id} my width changed: {newWidth}");
            this.previousSize.Width = newWidth;
            widthChanged = true;
        }

        if (newHeight != this.previousSize.Height)
        {
            Debug.WriteLine($"DockPanel {dock.Id} my height changed: {newHeight}");
            this.previousSize.Height = newHeight;
            heightChanged = true;
        }

        docker.ResizeDock(dock, widthChanged ? new Width(newWidth) : null, heightChanged ? new Height(newHeight) : null);
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
