// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Docking.Workspace;

/// <summary>
/// Represents a Dock , which holds a <see cref="Dockable" />, and must be inside a <see cref="LayoutDockGroup" />.
/// </summary>
/// <remarks>
/// Dock is an abstract type. Instances of a concrete `Dock`, can only be created through its <see cref="Factory" />, to ensure
/// that each one of them has a unique ID.
/// <para>
/// The <c>Factory</c> itself is internal use only. However, each concrete `Dock` class will offer a simple and straightforward
/// way to create instances of that specific type. Internally, the implementation will have to use the <c>Factory</c>.
/// <example>
/// <code>
/// var dock = ToolDock.New();
/// </code>
/// </example>
/// </para>
/// </remarks>
public abstract partial class Dock : IDock
{
    private readonly ObservableCollection<IDockable> dockables = [];
    private bool disposed;
    private Anchor? anchor;
    private Width width = new();
    private Height height = new();

    protected Dock() => this.Dockables = new ReadOnlyObservableCollection<IDockable>(this.dockables);

    public ReadOnlyObservableCollection<IDockable> Dockables { get; }

    public IDockable? ActiveDockable { get; private set; }

    public virtual bool CanMinimize => true;

    public virtual bool CanClose => true;

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

    public DockingState State { get; internal set; } = DockingState.Undocked;

    public DockId Id { get; private set; }

    public IDocker? Docker { get; internal set; }

    /// <summary>Gets a value representing the width of the Dock.</summary>
    /// <remarks>
    /// Changing the width of a dock will also change its active dockable height.
    /// <para>This operation should only be done via the <see cref="IDocker.ResizeDock" /> method.</para>
    /// </remarks>
    public Width Width
    {
        get => this.width;
        internal set
        {
            Debug.WriteLine($"Dock {this} my width has changed to: {value}");

            // Set the width for the Dock, but also for its Active Dockable
            this.width = value;
            if (this.ActiveDockable != null)
            {
                this.ActiveDockable.PreferredWidth = value;
            }
        }
    }

    /// <summary>Gets a value representing the height of the Dock.</summary>
    /// <remarks>
    /// Changing the height of a dock will also change its active dockable height.
    /// <para>This operation should only be done via the <see cref="IDocker.ResizeDock" /> method.</para>
    /// </remarks>
    public Height Height
    {
        get => this.height;
        internal set
        {
            Debug.WriteLine($"Dock {this} my height has changed to: {value}");

            // Set the height for the Dock, but also for its Active Dockable
            this.height = value;
            if (this.ActiveDockable != null)
            {
                this.ActiveDockable.PreferredHeight = value;
            }
        }
    }

    public string GroupInfo => this.Group?.ToString() ?? string.Empty;

    internal DockGroup? Group { get; set; }

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

        // If currently the values of Width or Height are null, the use the
        // preferred values from the dockable just added.
        if (this.width.IsNullOrEmpty && !dockableImpl.PreferredWidth.IsNullOrEmpty)
        {
            Debug.WriteLine(
                $"Dock {this} initializing my width from dockable {dockableImpl.Id}: {dockableImpl.PreferredWidth}");
            this.width = dockableImpl.PreferredWidth;
        }

        if (this.height.IsNullOrEmpty && !dockableImpl.PreferredHeight.IsNullOrEmpty)
        {
            Debug.WriteLine(
                $"Dock {this} initializing my height from dockable {dockableImpl.Id}: {dockableImpl.PreferredHeight}");
            this.height = dockableImpl.PreferredHeight;
        }
    }

    public void DestroyDockable(IDockable dockable)
    {
        var dockableImpl = dockable.AsDockable();

        var found = this.dockables.Remove(dockableImpl);
        Debug.Assert(found, $"dockable to be disowned ({dockableImpl}) should be managed by me ({this})");

        dockableImpl.PropertyChanged -= this.OnDockablePropertyChanged;
        dockableImpl.Dispose();
    }

    public void DisownDockable(IDockable dockable)
    {
        var dockableImpl = dockable.AsDockable();

        var found = this.dockables.Remove(dockableImpl);
        Debug.Assert(found, $"dockable to be disowned ({dockableImpl}) should be managed by me ({this})");
    }

    public void MigrateDockables(IDock destinationDock)
    {
        foreach (var dockable in this.dockables)
        {
            dockable.AsDockable().PropertyChanged -= this.OnDockablePropertyChanged;
            destinationDock.AdoptDockable(dockable);
        }

        this.dockables.Clear();
    }

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        foreach (var dockable in this.dockables)
        {
            dockable.AsDockable().PropertyChanged -= this.OnDockablePropertyChanged;
            dockable.Dispose();
        }

        // Reset the anchor only after the dockables are disposed of. Our anchor
        // maybe used to anchor the dockables that were anchored relative to our
        // dockables.
        this.anchor = null;

        this.Group = null;
        this.Docker = null;

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    public override string ToString() => $"{this.Id}";

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
#if DEBUG
            if (!dockable.IsActive)
            {
                Debug.WriteLine($"cannot set the only dockable `{dockable}` this.Id `{this}` have as inactive.");
            }
#endif
            dockable.IsActive = true;
            this.ActiveDockable = dockable;
            return;
        }

        // If the dockable is becoming active, deactivate the current active dockable.
        if (dockable.IsActive)
        {
            if (this.ActiveDockable != null)
            {
                this.ActiveDockable.AsDockable().IsActive = false;
            }

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
