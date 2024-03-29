// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Docking;

/// <summary>
/// Represents a Dock , which holds a <see cref="Dockable" />, and must be inside a <see cref="DockGroup" />.
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
    private readonly List<Dockable> dockables = [];
    private Dockable? activeDockable;

    private bool disposed;
    private Anchor? anchor;
    private Width width = new();
    private Height height = new();

    public ReadOnlyCollection<IDockable> Dockables => this.dockables.Cast<IDockable>().ToList().AsReadOnly();

    public IDockable? ActiveDockable => this.activeDockable;

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

    internal DockGroup? Group { get; set; }

    public virtual void AddDockable(Dockable dockable, DockablePlacement position = DockablePlacement.First)
    {
        dockable.Owner = this;
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
                var activeIndex = this.dockables.FindIndex(d => d.IsActive);
                index = activeIndex == -1
                    ? 0
                    : position == DockablePlacement.BeforeActiveItem
                        ? activeIndex
                        : activeIndex + 1;
                break;

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(DockablePlacement));
        }

        this.dockables.Insert(index, dockable);

        // Subscribe to changes to the IsActive property of the dockable.
        // Then set the dockable to active.
        dockable.PropertyChanged += this.OnDockablePropertyChanged;
        dockable.IsActive = true;

        // If currently the values of Width or Height are null, the use the
        // preferred values from the dockable just added.
        if (this.width.IsNullOrEmpty && !dockable.PreferredWidth.IsNullOrEmpty)
        {
            Debug.WriteLine(
                $"Dock {this} initializing my width from dockable {dockable.Id}: {dockable.PreferredWidth}");
            this.width = dockable.PreferredWidth;
        }

        if (this.height.IsNullOrEmpty && !dockable.PreferredHeight.IsNullOrEmpty)
        {
            Debug.WriteLine(
                $"Dock {this} initializing my height from dockable {dockable.Id}: {dockable.PreferredHeight}");
            this.height = dockable.PreferredHeight;
        }
    }

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        foreach (var dockable in this.dockables)
        {
            dockable.Dispose();
        }

        // Reset the anchor only after the dockables are disposed of. Our anchor
        // maybe used to anchor the dockables that were anchored relative to our
        // dockables.
        this.anchor = null;

        this.dockables.Clear();
        this.Group = null;
        this.Docker = null;

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    public override string ToString() => $"{this.Id}";

    private void OnDockablePropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName != nameof(Dockable.IsActive))
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
            this.activeDockable = dockable;
            return;
        }

        // If the dockable is becoming active, deactivate the current active dockable.
        if (dockable.IsActive)
        {
            if (this.activeDockable != null)
            {
                this.activeDockable.IsActive = false;
            }

            this.activeDockable = dockable;
        }

        // If the active dockable is becoming inactive, activate another dockable.
        else if (this.activeDockable == dockable)
        {
            var other = this.dockables.First(d => d != dockable);
            other.IsActive = true;
            this.activeDockable = other;
        }
    }
}
