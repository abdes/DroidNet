// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Windows.Foundation;

namespace DroidNet.Docking.Controls;

/// <summary>
/// The ViewModel for a dock panel.
/// </summary>
/// <remarks>
/// The <see cref="DockPanelViewModel"/> class provides the data and commands necessary to manage the state and behavior of a dock panel. It supports operations such as toggling docking mode, minimizing, and closing the dock.
/// </remarks>
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

    /// <summary>
    /// Initializes a new instance of the <see cref="DockPanelViewModel"/> class.
    /// </summary>
    /// <param name="dock">The dock instance to be managed by this ViewModel.</param>
    /// <remarks>
    /// This constructor sets up the ViewModel with the specified dock and initializes its properties.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var dock = new CustomDock();
    /// var viewModel = new DockPanelViewModel(dock);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public DockPanelViewModel(IDock dock)
    {
        Debug.Assert(dock.Docker is not null, "expecting a docked dock to have a valid docker property");

        this.dock = dock;
        this.docker = dock.Docker;
        this.title = dock.ToString() ?? "EMPTY";
        this.Dockables = dock.Dockables;
    }

    /// <summary>
    /// Gets the collection of dockables in the dock.
    /// </summary>
    /// <value>
    /// A read-only collection of <see cref="IDockable"/> items in the dock.
    /// </value>
    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    /// <summary>
    /// Handles the size changed event for the dock panel.
    /// </summary>
    /// <param name="newSize">The new size of the dock panel.</param>
    /// <remarks>
    /// This method updates the size of the dock in the docker. It ensures that the size is only updated after the initial size update.
    /// </remarks>
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

    /// <summary>
    /// Called when the view model is activated.
    /// </summary>
    /// <remarks>
    /// This method registers the view model to listen for docking mode messages.
    /// It ensures that the view model can participate in docking maneuvers by handling
    /// <see cref="EnterDockingModeMessage"/> and <see cref="LeaveDockingModeMessage"/>.
    /// </remarks>
    /// <seealso cref="EnterDockingModeMessage"/>
    /// <seealso cref="LeaveDockingModeMessage"/>
    protected override void OnActivated()
    {
        base.OnActivated();

        // Listen for the docking mode messages to participate during a docking maneuver.
        StrongReferenceMessenger.Default.Register<EnterDockingModeMessage>(
            this,
            (_, message) => this.EnterDockingMode(message.Value));
        StrongReferenceMessenger.Default.Register<LeaveDockingModeMessage>(this, this.LeaveDockingMode);
    }

    private static AnchorPosition AnchorPositionFromString(string position) => Enum.TryParse<AnchorPosition>(position, ignoreCase: true, out var result)
            ? result
            : throw new ArgumentException($"invalid anchor position for root docking `{position}`", nameof(position));

    /// <summary>
    /// Toggles the docking mode.
    /// </summary>
    /// <remarks>
    /// This command toggles the docking mode for the dock panel. If the panel is in docking mode, it sends a <see cref="LeaveDockingModeMessage"/>; otherwise, it sends an <see cref="EnterDockingModeMessage"/>.
    /// </remarks>
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

    /// <summary>
    /// Accepts the dock being docked at the specified anchor position.
    /// </summary>
    /// <param name="anchorPosition">The anchor position where the dock should be docked.</param>
    /// <remarks>
    /// This command docks the dock being docked at the specified anchor position and then toggles the docking mode.
    /// </remarks>
    [RelayCommand]
    private void AcceptDockBeingDocked(string anchorPosition)
    {
        if (this.dockBeingDocked is null)
        {
            return;
        }

        var anchor = new Anchor(AnchorPositionFromString(anchorPosition), this.dock.ActiveDockable);
        try
        {
            this.docker.Dock(this.dockBeingDocked, anchor);
            anchor = null; // Dispose ownership transferred
            this.ToggleDockingMode();
        }
        finally
        {
            anchor?.Dispose();
        }
    }

    /// <summary>
    /// Docks the dock to the root at the specified anchor position.
    /// </summary>
    /// <param name="anchorPosition">The anchor position where the dock should be docked.</param>
    /// <remarks>
    /// This command docks the dock to the root at the specified anchor position and then toggles the docking mode.
    /// </remarks>
    [RelayCommand]
    private void DockToRoot(string anchorPosition)
    {
        var anchor = new Anchor(AnchorPositionFromString(anchorPosition));

        try
        {
            this.docker.Dock(this.dock, anchor);
            anchor = null; // Dispose ownership transferred
            this.ToggleDockingMode();
        }
        finally
        {
            anchor?.Dispose();
        }
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

    /// <summary>
    /// Minimizes the dock.
    /// </summary>
    /// <remarks>
    /// This command minimizes the dock by calling the <see cref="IDocker.MinimizeDock"/> method.
    /// </remarks>
    [RelayCommand(CanExecute = nameof(CanMinimize))]
    private void Minimize() => this.docker.MinimizeDock(this.dock);

    private bool CanMinimize() => !this.IsInDockingMode;

    /// <summary>
    /// Closes the dock.
    /// </summary>
    /// <remarks>
    /// This command closes the dock by calling the <see cref="IDocker.CloseDock"/> method and unregisters all messages for this ViewModel.
    /// </remarks>
    [RelayCommand(CanExecute = nameof(CanClose))]
    private void Close()
    {
        StrongReferenceMessenger.Default.UnregisterAll(this);
        this.docker.CloseDock(this.dock);
    }

    private bool CanClose() => !this.IsInDockingMode && this.dock.CanClose;
}
