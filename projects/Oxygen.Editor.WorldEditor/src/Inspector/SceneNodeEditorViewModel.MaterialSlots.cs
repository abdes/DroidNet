// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Refreshes material picker values when selected override slots change.</summary>
public sealed partial class SceneNodeEditorViewModel
{
    private readonly HashSet<GeometryComponent> observedGeometries = [];
    private readonly HashSet<OverrideSlot> observedMaterialSlots = [];

    private void SubscribeMaterialSlots(GeometryComponent geometry)
    {
        if (this.observedGeometries.Add(geometry))
        {
            geometry.OverrideSlots.CollectionChanged += this.OnMaterialSlotsChanged;
        }

        foreach (var slot in geometry.OverrideSlots)
        {
            if (this.observedMaterialSlots.Add(slot))
            {
                slot.PropertyChanged += this.OnSelectedComponentPropertyChanged;
            }
        }
    }

    private void UnsubscribeMaterialSlots()
    {
        foreach (var geometry in this.observedGeometries)
        {
            geometry.OverrideSlots.CollectionChanged -= this.OnMaterialSlotsChanged;
        }

        foreach (var slot in this.observedMaterialSlots)
        {
            slot.PropertyChanged -= this.OnSelectedComponentPropertyChanged;
        }

        this.observedGeometries.Clear();
        this.observedMaterialSlots.Clear();
    }

    private void OnMaterialSlotsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        var geometries = this.observedGeometries.ToArray();
        this.UnsubscribeMaterialSlots();
        foreach (var geometry in geometries)
        {
            this.SubscribeMaterialSlots(geometry);
        }

        this.OnSelectedComponentPropertyChanged(sender, new(nameof(GeometryComponent.OverrideSlots)));
    }
}
