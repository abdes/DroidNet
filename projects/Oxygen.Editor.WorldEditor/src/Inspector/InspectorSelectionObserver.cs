// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns selected component and material-slot observation, suppressing queued work from old selections.</summary>
internal sealed class InspectorSelectionObserver(DispatcherQueue? dispatcher, Action<InspectorSelectionChange> changed) : IDisposable
{
    private readonly HashSet<INotifyCollectionChanged> collections = [];
    private readonly HashSet<GameComponent> components = [];
    private readonly HashSet<GeometryComponent> geometries = [];
    private readonly HashSet<OverrideSlot> slots = [];
    private int generation;
    private bool disposed;

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.Detach();
    }

    /// <summary>Replaces selected subscriptions and invalidates queued old-selection callbacks.</summary>
    /// <param name="nodes">The current selected nodes.</param>
    internal void Bind(IEnumerable<SceneNode> nodes)
    {
        this.Detach();
        if (this.disposed)
        {
            return;
        }

        foreach (var node in nodes)
        {
            if (this.collections.Add(node.Components))
            {
                node.Components.CollectionChanged += this.OnComponentsChanged;
            }

            foreach (var component in node.Components)
            {
                if (this.components.Add(component))
                {
                    component.PropertyChanged += this.OnComponentChanged;
                }

                if (component is GeometryComponent geometry && this.geometries.Add(geometry))
                {
                    geometry.OverrideSlots.CollectionChanged += this.OnSlotsChanged;
                }
            }
        }

        this.AttachSlots();
    }

    private void Detach()
    {
        this.generation++;
        foreach (var collection in this.collections)
        {
            collection.CollectionChanged -= this.OnComponentsChanged;
        }

        foreach (var component in this.components)
        {
            component.PropertyChanged -= this.OnComponentChanged;
        }

        foreach (var geometry in this.geometries)
        {
            geometry.OverrideSlots.CollectionChanged -= this.OnSlotsChanged;
        }

        this.DetachSlots();
        this.collections.Clear();
        this.components.Clear();
        this.geometries.Clear();
    }

    private void AttachSlots()
    {
        foreach (var slot in this.geometries.SelectMany(static geometry => geometry.OverrideSlots))
        {
            if (this.slots.Add(slot))
            {
                slot.PropertyChanged += this.OnSlotChanged;
            }
        }
    }

    private void DetachSlots()
    {
        foreach (var slot in this.slots)
        {
            slot.PropertyChanged -= this.OnSlotChanged;
        }

        this.slots.Clear();
    }

    private void OnComponentsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (sender is INotifyCollectionChanged collection && this.collections.Contains(collection))
        {
            this.Notify(InspectorSelectionChange.Structure);
        }
    }

    private void OnComponentChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is GameComponent component && this.components.Contains(component))
        {
            this.Notify(InspectorSelectionChange.Values);
        }
    }

    private void OnSlotsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        this.DetachSlots();
        this.AttachSlots();
        this.Notify(InspectorSelectionChange.Values);
    }

    private void OnSlotChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is OverrideSlot slot && this.slots.Contains(slot))
        {
            this.Notify(InspectorSelectionChange.Values);
        }
    }

    private void Notify(InspectorSelectionChange change)
    {
        var lifetime = this.generation;
        void Deliver()
        {
            if (!this.disposed && lifetime == this.generation)
            {
                changed(change);
            }
        }

        if (dispatcher is { HasThreadAccess: false })
        {
            _ = dispatcher.TryEnqueue(Deliver);
        }
        else
        {
            Deliver();
        }
    }
}
