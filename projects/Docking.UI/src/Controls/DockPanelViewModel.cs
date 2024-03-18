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
public partial class DockPanelViewModel : ObservableObject
{
    private readonly IDock dock;
    private readonly IDocker docker;

    private bool initialSizeUpdate = true;
    private Size previousSize;

    [ObservableProperty]
    private string title;

    public DockPanelViewModel(IDock dock)
    {
        Debug.Assert(dock.Docker is not null, "expecting a docked dock to have a valid docker property");

        this.dock = dock;
        this.docker = dock.Docker;
        this.title = dock.ToString() ?? "EMPTY";
        this.Dockables
            = new ReadOnlyObservableCollection<IDockable>(new ObservableCollection<IDockable>(dock.Dockables));
    }

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    public void OnSizeChanged(Size newSize)
    {
        // If this is the initial size update, we memorize it, but we do not trigger a resize of the dock. We only
        // trigger the resize if the size changes after the initial update.
        if (this.initialSizeUpdate)
        {
            Debug.WriteLine($"DockPanel {this.dock.Id} this is our initial size: {newSize}");
            this.previousSize.Width = newSize.Width;
            this.previousSize.Height = newSize.Height;
            this.initialSizeUpdate = false;
            return;
        }

        var (widthChanged, heightChanged) = this.SizeReallyChanged(newSize);

        this.docker.ResizeDock(
            this.dock,
            widthChanged ? new Width(newSize.Width) : null,
            heightChanged ? new Height(newSize.Height) : null);
    }

    private (bool widthChanged, bool heightChanged) SizeReallyChanged(Size newSize)
        => (Math.Abs(newSize.Width - this.previousSize.Width) > 0.5,
            Math.Abs(newSize.Height - this.previousSize.Height) > 0.5);

    [RelayCommand]
    private void TogglePinned()
    {
        switch (this.dock.State)
        {
            case DockingState.Minimized:
            case DockingState.Floating:
                this.docker.PinDock(this.dock);
                break;

            case DockingState.Pinned:
                this.docker.MinimizeDock(this.dock);
                break;

            case DockingState.Undocked:
            default:
                throw new InvalidOperationException(
                    $"attempt to toggle pinned state of a dock while it is not in a valid state: {this.dock.State}");
        }
    }

    [RelayCommand(CanExecute = nameof(CanClose))]
    private void Close() => this.docker.CloseDock(this.dock);

    [RelayCommand]
    private void AddDockable(Dockable dockable) => this.dock.AddDockable(dockable);

    private bool CanClose() => this.dock.CanClose;
}
