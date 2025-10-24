// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Detail;

/// <summary>
/// Represents a Dock, which holds a <see cref="Dockable"/>, and must be inside a <see cref="LayoutDockGroup"/>.
/// </summary>
/// <remarks>
/// <para>
/// Dock is an abstract type. Instances of a concrete `Dock` can only be created through its <see cref="Factory"/>, to ensure
/// that each one of them has a unique ID.
/// </para>
/// <para>
/// The <c>Factory</c> itself is for internal use only. However, each concrete `Dock` class will offer a simple and straightforward
/// way to create instances of that specific type. Internally, the implementation will have to use the <c>Factory</c>.
/// </para>
/// <example>
/// <code><![CDATA[
/// var dock = ToolDock.New();
/// ]]></code>
/// </example>
/// </remarks>
public abstract partial class Dock : IDock
{
    private readonly ObservableCollection<IDockable> dockables = [];
    private bool isDisposed;
    private Anchor? anchor;
    private Width width = new();
    private Height height = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="Dock"/> class.
    /// </summary>
    protected Dock()
    {
        this.Dockables = new ReadOnlyObservableCollection<IDockable>(this.dockables);
    }

    /// <inheritdoc/>
    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    /// <inheritdoc/>
    public IDockable? ActiveDockable { get; private set; }

    /// <inheritdoc/>
    public virtual bool CanMinimize => true;

    /// <inheritdoc/>
    public virtual bool CanClose => true;

    /// <inheritdoc/>
    public Anchor? Anchor
    {
        get => this.anchor;
        internal set
        {
            // Dispose of the old anchor if we had one
            this.anchor?.Dispose();
            this.anchor = value;
        }
    }

    /// <inheritdoc/>
    public DockingState State { get; internal set; } = DockingState.Undocked;

    /// <inheritdoc/>
    public DockId Id { get; private set; }

    /// <inheritdoc/>
    public IDocker? Docker { get; internal set; }

    /// <summary>
    /// Gets a value representing the width of the Dock.
    /// </summary>
    /// <remarks>
    /// Changing the width of a dock will also change its active dockable width.
    /// <para>This operation should only be done via the <see cref="IDocker.ResizeDock"/> method.</para>
    /// </remarks>
    public Width Width
    {
        get => this.width;
        internal set
        {
            this.width = value;
            _ = this.ActiveDockable?.PreferredWidth = value;
        }
    }

    /// <summary>
    /// Gets a value representing the height of the Dock.
    /// </summary>
    /// <remarks>
    /// Changing the height of a dock will also change its active dockable height.
    /// <para>This operation should only be done via the <see cref="IDocker.ResizeDock"/> method.</para>
    /// </remarks>
    public Height Height
    {
        get => this.height;
        internal set
        {
            this.height = value;
            _ = this.ActiveDockable?.PreferredHeight = value;
        }
    }

    /// <summary>
    /// Gets information about the group to which this dock belongs.
    /// </summary>
    public string GroupInfo => this.Group?.ToString() ?? string.Empty;

    /// <summary>
    /// Gets or sets the group to which this dock belongs.
    /// </summary>
    internal DockGroup? Group { get; set; }

    /// <inheritdoc/>
    public virtual void AdoptDockable(IDockable dockable, DockablePlacement position = DockablePlacement.Last)
    {
        var dockableImpl = dockable.AsDockable();

        dockableImpl.Owner = this;
        int index;
        switch (position)
        {
            case DockablePlacement.First:
                index = 0;
                break;

            case DockablePlacement.Last:
                index = this.dockables.Count;
                break;

            case DockablePlacement.AfterActiveItem:
            case DockablePlacement.BeforeActiveItem:
                index = 0;
                for (var activeIndex = 0; activeIndex < this.dockables.Count; activeIndex++)
                {
                    if (this.Dockables[activeIndex].IsActive)
                    {
                        index = position == DockablePlacement.BeforeActiveItem ? activeIndex : activeIndex + 1;
                        break;
                    }
                }

                break;

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(DockablePlacement));
        }

        this.dockables.Insert(index, dockableImpl);

        // Subscribe to changes to the IsActive property of the dockable.
        // Then set the dockable to active.
        dockableImpl.PropertyChanged += this.OnDockablePropertyChanged;
        dockableImpl.IsActive = true;

        // If currently the values of Width or Height are null, use the
        // preferred values from the dockable just added.
        if (this.width.IsNullOrEmpty && !dockableImpl.PreferredWidth.IsNullOrEmpty)
        {
            this.width = dockableImpl.PreferredWidth;
        }

        if (this.height.IsNullOrEmpty && !dockableImpl.PreferredHeight.IsNullOrEmpty)
        {
            this.height = dockableImpl.PreferredHeight;
        }
    }

    /// <inheritdoc/>
    public void DestroyDockable(IDockable dockable)
    {
        var dockableImpl = dockable.AsDockable();

        var found = this.dockables.Remove(dockableImpl);
        Debug.Assert(found, $"dockable to be disowned ({dockableImpl}) should be managed by me ({this})");

        dockableImpl.PropertyChanged -= this.OnDockablePropertyChanged;
        dockableImpl.Dispose();
    }

    /// <inheritdoc/>
    public void DisownDockable(IDockable dockable)
    {
        var dockableImpl = dockable.AsDockable();

        var found = this.dockables.Remove(dockableImpl);
        Debug.Assert(found, $"dockable to be disowned ({dockableImpl}) should be managed by me ({this})");
    }

    /// <inheritdoc/>
    public void MigrateDockables(IDock destinationDock)
    {
        foreach (var dockable in this.dockables)
        {
            dockable.AsDockable().PropertyChanged -= this.OnDockablePropertyChanged;
            destinationDock.AdoptDockable(dockable);
        }

        this.dockables.Clear();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc/>
    public override string ToString() => $"{this.Id}";

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="Dock"/> and optionally releases the
    /// managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/>
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            // Dispose of managed resources
            foreach (var dockable in this.dockables)
            {
                dockable.AsDockable().PropertyChanged -= this.OnDockablePropertyChanged;
                dockable.Dispose();
            }

            // Reset the anchor only after the dockables are disposed of. Our anchor may be used to
            // anchor the dockables that were anchored relative to our dockables.
            this.anchor = null;

            this.Group = null;
            this.Docker = null;
        }

        // Dispose of unmanaged resources
        this.isDisposed = true;
    }

    private void OnDockablePropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(Dockable.IsActive), StringComparison.Ordinal))
        {
            return;
        }

        var dockable = (Dockable)sender!;

        // We must always have an active dockable. If this is the only
        // dockable we have, force it to stay active.
        if (this.dockables.Count == 1)
        {
            dockable.IsActive = true;
            this.ActiveDockable = dockable;
            return;
        }

        // If the dockable is becoming active, deactivate the current active dockable.
        if (dockable.IsActive)
        {
            _ = this.ActiveDockable?.AsDockable().IsActive = false;
            this.ActiveDockable = dockable;
        }

        // If the active dockable is becoming inactive, activate another dockable.
        else if (this.ActiveDockable == dockable)
        {
            var other = this.dockables.First(d => d != dockable);
            other.AsDockable().IsActive = true;
            this.ActiveDockable = other;
        }
    }
}
