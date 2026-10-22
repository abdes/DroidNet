// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Workspace;

/// <summary>The docks and dockables this docker creates, and its docking maneuver.</summary>
public partial class Docker
{
    private readonly Dictionary<string, Dockable> dockables = new(StringComparer.Ordinal);
    private uint lastDockId;
    private IDock? dockBeingDocked;

    /// <inheritdoc/>
    public event EventHandler<DockingModeChangedEventArgs>? DockingModeChanged;

    /// <inheritdoc/>
    public IReadOnlyCollection<Dockable> Dockables => this.dockables.Values;

    /// <inheritdoc/>
    public Dockable? FindDockable(string id) => this.dockables.GetValueOrDefault(id);

    /// <inheritdoc/>
    public TDock CreateDock<TDock>(params object[] args)
        where TDock : Dock
    {
        var dock = CreateInstance<TDock>(args);
        dock.Id = new DockId(++this.lastDockId);
        return dock;
    }

    /// <inheritdoc/>
    public TDockable CreateDockable<TDockable>(string id, params object[] args)
        where TDockable : Dockable
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        if (this.dockables.ContainsKey(id))
        {
            throw new InvalidOperationException($"attempt to create a dockable with an already used ID: {id}");
        }

        var dockable = CreateInstance<TDockable>([id, .. args]);
        this.dockables.Add(id, dockable);
        dockable.Release = this.ReleaseDockable;
        return dockable;
    }

    /// <inheritdoc/>
    public void EnterDockingMode(IDock dock)
    {
        ArgumentNullException.ThrowIfNull(dock);
        this.dockBeingDocked = dock;
        this.DockingModeChanged?.Invoke(this, new DockingModeChangedEventArgs(dock));
    }

    /// <inheritdoc/>
    public void LeaveDockingMode()
    {
        if (this.dockBeingDocked is null)
        {
            return;
        }

        this.dockBeingDocked = null;
        this.DockingModeChanged?.Invoke(this, new DockingModeChangedEventArgs(dockBeingDocked: null));
    }

    private static T CreateInstance<T>(object[] args)
    {
        object? instance;
        try
        {
            instance = Activator.CreateInstance(
                typeof(T),
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                binder: null,
                args,
                culture: null);
        }
        catch (Exception ex)
        {
            throw new ObjectCreationException(message: null, ex) { ObjectType = typeof(T) };
        }

        return instance is T created ? created : throw new ObjectCreationException { ObjectType = typeof(T) };
    }

    /// <summary>Disposes the dockables that no dock owns; the docking tree disposes the others with their docks.</summary>
    private void DisposeDockables()
    {
        foreach (var dockable in this.dockables.Values.ToList())
        {
            dockable.Dispose();
        }
    }

    private void ReleaseDockable(Dockable dockable) => _ = this.dockables.Remove(dockable.Id);
}
