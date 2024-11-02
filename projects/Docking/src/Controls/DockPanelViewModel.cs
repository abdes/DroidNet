// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Docking;
using Windows.Foundation;

/// <summary>The ViewModel for a dock panel.</summary>
public partial class DockPanelViewModel : ObservableRecipient
{
    private readonly IDocker docker;
    private readonly IDock dock;

    private bool initialSizeUpdate = true;
    private Size previousSize;

    [ObservableProperty]
    private string title;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ToggleDockingModeCommand))]
    [NotifyCanExecuteChangedFor(nameof(MinimizeCommand))]
    [NotifyCanExecuteChangedFor(nameof(CloseCommand))]
    private bool isInDockingMode;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ToggleDockingModeCommand))]
    private bool isBeingDocked;

    private IDock? dockBeingDocked;

    public DockPanelViewModel(IDock dock)
    {
        Debug.Assert(dock.Docker is not null, "expecting a docked dock to have a valid docker property");

        this.dock = dock;
        this.docker = dock.Docker;
        this.title = dock.ToString() ?? "EMPTY";
        this.Dockables = dock.Dockables;
    }

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    public void OnSizeChanged(Size newSize)
    {
        // If this is the initial size update, we memorize it, but we do not trigger a resize of the dock. We only
        // trigger the resize if the size changes after the initial update.
        if (this.initialSizeUpdate)
        {
            // $"DockPanel {this.dock.Id} this is our initial size: {newSize}"
            this.previousSize.Width = newSize.Width;
            this.previousSize.Height = newSize.Height;
            this.initialSizeUpdate = false;

            /* FIXME: investigate why after the panel is loaded its size is 1 pixel less than requested */

            this.docker.ResizeDock(this.dock, new Width(newSize.Width), new Height(newSize.Height));
            return;
        }

        var (widthChanged, heightChanged) = this.SizeReallyChanged(newSize);

        this.docker.ResizeDock(
            this.dock,
            widthChanged ? new Width(newSize.Width) : null,
            heightChanged ? new Height(newSize.Height) : null);
    }

    protected override void OnActivated()
    {
        base.OnActivated();

        // Listen for the docking mode messages to participate during a docking manoeuvre.
        StrongReferenceMessenger.Default.Register<EnterDockingModeMessage>(
            this,
            (_, message) => this.EnterDockingMode(message.Value));
        StrongReferenceMessenger.Default.Register<LeaveDockingModeMessage>(this, this.LeaveDockingMode);
    }

    private static AnchorPosition AnchorPositionFromString(string anchorPosition)
        => anchorPosition.ToLowerInvariant() switch
        {
            "left" => AnchorPosition.Left,
            "right" => AnchorPosition.Right,
            "top" => AnchorPosition.Top,
            "bottom" => AnchorPosition.Bottom,
            "with" => AnchorPosition.With,
            "center" => AnchorPosition.Center,
            _ => throw new ArgumentException(
                $"invalid anchor position for root docking `{anchorPosition}`",
                anchorPosition),
        };

    [RelayCommand(CanExecute = nameof(CanToggleDockingMode))]
    private void ToggleDockingMode()
    {
        if (this.IsInDockingMode)
        {
            _ = StrongReferenceMessenger.Default.Send(new LeaveDockingModeMessage());
        }
        else
        {
            _ = StrongReferenceMessenger.Default.Send(new EnterDockingModeMessage(this.dock));
        }
    }

    [RelayCommand]
    private void AcceptDockBeingDocked(string anchorPosition)
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPositionFromString(anchorPosition), this.dock.ActiveDockable));
        this.ToggleDockingMode();
    }

    [RelayCommand]
    private void DockToRoot(string anchorPosition)
    {
        var anchor = new Anchor(AnchorPositionFromString(anchorPosition));

        this.docker.Dock(this.dock, anchor);
        this.ToggleDockingMode();
    }

    private void AnchorDockBeingDockedAt(Anchor anchor)
    {
        if (this.dockBeingDocked is null)
        {
            return;
        }

        this.docker.Dock(this.dockBeingDocked, anchor);
    }

    private void LeaveDockingMode(object recipient, LeaveDockingModeMessage message)
    {
        this.IsBeingDocked = false;
        this.IsInDockingMode = false;
        this.dockBeingDocked = null;
    }

    private void EnterDockingMode(IDock forDock)
    {
        this.dockBeingDocked = forDock;
        this.IsBeingDocked = forDock == this.dock; // set this property before IsInDockingMode
        this.IsInDockingMode = true;
    }

    private (bool widthChanged, bool heightChanged) SizeReallyChanged(Size newSize)
        => (Math.Abs(newSize.Width - this.previousSize.Width) > 0.5,
            Math.Abs(newSize.Height - this.previousSize.Height) > 0.5);

    private bool CanToggleDockingMode() => !this.IsInDockingMode || this.IsBeingDocked;

    [RelayCommand(CanExecute = nameof(CanMinimize))]
    private void Minimize() => this.docker.MinimizeDock(this.dock);

    private bool CanMinimize() => !this.IsInDockingMode;

    [RelayCommand(CanExecute = nameof(CanClose))]
    private void Close()
    {
        StrongReferenceMessenger.Default.UnregisterAll(this);
        this.docker.CloseDock(this.dock);
    }

    private bool CanClose() => !this.IsInDockingMode && this.dock.CanClose;
}
