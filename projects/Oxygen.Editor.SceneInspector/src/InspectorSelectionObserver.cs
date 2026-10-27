// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Owns selected component observation, overrides included, suppressing queued work from old selections.</summary>
/// <remarks>
/// It also observes each selected node's ancestors' flags and hierarchy, which decide the values its Inherit node flags
/// resolve to, and follows a reparent to the new ancestors.
/// </remarks>
internal sealed partial class InspectorSelectionObserver(DispatcherQueue? dispatcher, Action<InspectorSelectionChange> changed) : IDisposable
{
    private readonly HashSet<INotifyCollectionChanged> collections = [];
    private readonly HashSet<GameComponent> components = [];
    private IReadOnlyList<SceneNode> nodes = [];
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

        this.nodes = [.. nodes];
        foreach (var node in this.nodes)
        {
            if (this.collections.Add(node.Components))
            {
                node.Components.CollectionChanged += this.OnComponentsChanged;
            }

            // A component replaces its overrides as a whole, so its own change covers them.
            foreach (var component in node.Components)
            {
                this.Observe(component);
            }

            for (var ancestor = node.Hierarchy.Parent; ancestor is not null; ancestor = ancestor.Hierarchy.Parent)
            {
                this.Observe(ancestor.Flags);
                this.Observe(ancestor.Hierarchy);
            }
        }
    }

    private void Observe(GameComponent component)
    {
        if (this.components.Add(component))
        {
            component.PropertyChanged += this.OnComponentChanged;
        }
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

        this.collections.Clear();
        this.components.Clear();
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
            // A reparent changes the ancestors whose flags resolve the selection's.
            if (component is HierarchyComponent)
            {
                this.Bind(this.nodes);
            }

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
