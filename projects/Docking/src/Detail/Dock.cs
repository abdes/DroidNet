// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using System.ComponentModel;
using DroidNet.Docking;

/// <summary>
/// Represents a Dock , which holds a <see cref="Docking.Dockable" />, and must be
/// inside a <see cref="DockGroup" />.
/// </summary>
/// <remarks>
/// Dock is an abstract type. Instances of a concrete `Dock`, can only be
/// created through its <see cref="Factory" />, to ensure that each one of them
/// has a unique ID.
/// <para>
/// The <c>Factory</c> itself is internal use only. However, each concrete
/// `Dock` class will offer a simple and straightforward way to create instances
/// of that specific type. Internally, the implementation will have to use the
/// <c>Factory</c>.
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

    private bool disposed;
    private Anchor? anchor;

    public ReadOnlyCollection<IDockable> Dockables => this.dockables.Cast<IDockable>().ToList().AsReadOnly();

    public IDockable? ActiveDockable
    {
        get => this.Dockables.FirstOrDefault(d => d.IsActive);
        set
        {
            var old = this.dockables.FirstOrDefault(d => d.IsActive);
            if (old != null)
            {
                old.IsActive = false;
            }

            if (value == null)
            {
                return;
            }

            var activate = this.dockables.FirstOrDefault(d => d.Id == value.Id) ??
                           throw new ArgumentException($"no such dock with ID `{value.Id}` in my collection");
            activate.IsActive = true;
        }
    }

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
        this.ActiveDockable = dockable;
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

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    public override string ToString() => $"{this.Id}";
}
