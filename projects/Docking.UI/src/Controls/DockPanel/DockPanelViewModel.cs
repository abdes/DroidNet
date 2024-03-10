// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

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
        // If this is the initial size update, we memorize it, but we do not trigger a resize of the dock. We only
        // trigger the resize if the size changes after the initial update.
        if (this.initialSizeUpdate)
        {
            Debug.WriteLine($"DockPanel {dock.Id} this is our initial size: {newSize}");
            this.previousSize.Width = newSize.Width;
            this.previousSize.Height = newSize.Height;
            this.initialSizeUpdate = false;
            return;
        }

        var (widthChanged, heightChanged) = this.SizeReallyChanged(newSize);

        docker.ResizeDock(
            dock,
            widthChanged ? new Width(newSize.Width) : null,
            heightChanged ? new Height(newSize.Height) : null);
    }

    private (bool widthChanged, bool heightChanged) SizeReallyChanged(Size newSize)
        => (Math.Abs(newSize.Width - this.previousSize.Width) > 0.5,
            Math.Abs(newSize.Height - this.previousSize.Height) > 0.5);

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
