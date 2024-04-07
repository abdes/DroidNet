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
using DroidNet.Docking.Utils;
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

    public void DockToRootLeft()
    {
        this.docker.DockToRoot(this.dock, AnchorPosition.Left);
        this.docker.Root.DumpGroup();
    }

    public void DockToRootTop()
    {
        this.docker.DockToRoot(this.dock, AnchorPosition.Top);
        this.docker.Root.DumpGroup();
    }

    public void DockToRootRight()
    {
        this.docker.DockToRoot(this.dock, AnchorPosition.Right);
        this.docker.Root.DumpGroup();
    }

    public void DockToRootBottom()
    {
        this.docker.DockToRoot(this.dock, AnchorPosition.Bottom);
        this.docker.Root.DumpGroup();
    }

    public void AcceptDockBeingDockedLeft()
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPosition.Left, this.dock.ActiveDockable));
        this.docker.Root.DumpGroup();
    }

    public void AcceptDockBeingDockedTop()
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPosition.Top, this.dock.ActiveDockable));
        this.docker.Root.DumpGroup();
    }

    public void AcceptDockBeingDockedRight()
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPosition.Right, this.dock.ActiveDockable));
        this.docker.Root.DumpGroup();
    }

    public void AcceptDockBeingDockedBottom()
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPosition.Bottom, this.dock.ActiveDockable));
        this.docker.Root.DumpGroup();
    }

    public void AcceptDockBeingDocked()
    {
        this.AnchorDockBeingDockedAt(new Anchor(AnchorPosition.With, this.dock.ActiveDockable));
        this.docker.Root.DumpGroup();
    }

    [RelayCommand(CanExecute = nameof(CanToggleDockingMode))]
    public void ToggleDockingMode()
    {
        if (this.IsInDockingMode)
        {
            _ = WeakReferenceMessenger.Default.Send(new LeaveDockingModeMessage());
        }
        else
        {
            _ = WeakReferenceMessenger.Default.Send(new EnterDockingModeMessage(this.dock));
        }
    }

    protected override void OnActivated()
    {
        base.OnActivated();

        // Listen for the docking mode messages to participate during a docking manoeuvre.
        WeakReferenceMessenger.Default.Register<EnterDockingModeMessage>(
            this,
            (_, message) => this.EnterDockingMode(message.Value));
        WeakReferenceMessenger.Default.Register<LeaveDockingModeMessage>(this, this.LeaveDockingMode);
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
        WeakReferenceMessenger.Default.UnregisterAll(this);
        this.docker.CloseDock(this.dock);
    }

    private bool CanClose() => !this.IsInDockingMode && this.dock.CanClose;
}
