// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Services;

/// <summary>Retires demand when references change, including Undo while the document is already dirty.</summary>
public sealed partial class SceneContentDemandService
{
    private readonly HashSet<INotifyPropertyChanged> referenceProperties = [];
    private readonly HashSet<INotifyCollectionChanged> referenceCollections = [];

    private void ObserveReferences()
    {
        if (this.scene is not { } current)
        {
            return;
        }

        this.ObserveCollection(current.RootNodes);
        foreach (var node in current.AllNodes)
        {
            this.ObserveCollection(node.Children);
            this.ObserveCollection(node.Components);
            foreach (var geometry in node.Components.OfType<GeometryComponent>())
            {
                this.ObserveProperty(geometry);
                this.ObserveCollection(geometry.OverrideSlots);
                foreach (var slot in geometry.OverrideSlots.OfType<MaterialsSlot>())
                {
                    this.ObserveProperty(slot);
                }
            }
        }
    }

    private void ObserveProperty(INotifyPropertyChanged source)
    {
        if (this.referenceProperties.Add(source))
        {
            source.PropertyChanged += this.OnReferenceChanged;
        }
    }

    private void ObserveCollection(object source)
    {
        if (source is INotifyCollectionChanged collection && this.referenceCollections.Add(collection))
        {
            collection.CollectionChanged += this.OnReferencesChanged;
        }
    }

    private void OnReferenceChanged(object? sender, PropertyChangedEventArgs args) => this.Dispatch(this.InvalidateObsoleteDemands);

    private void OnReferencesChanged(object? sender, NotifyCollectionChangedEventArgs args) => this.Dispatch(this.RefreshReferenceObservers);

    private void RefreshReferenceObservers()
    {
        this.RemoveReferenceObservers();
        this.ObserveReferences();
        this.InvalidateObsoleteDemands();
    }

    private void RemoveReferenceObservers()
    {
        foreach (var source in this.referenceProperties)
        {
            source.PropertyChanged -= this.OnReferenceChanged;
        }

        foreach (var source in this.referenceCollections)
        {
            source.CollectionChanged -= this.OnReferencesChanged;
        }

        this.referenceProperties.Clear();
        this.referenceCollections.Clear();
    }
}
