// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Removes material overrides from nodes' geometry components, such as the overrides no slot resolves anymore.</summary>
/// <remarks>
/// Overrides are matched by geometry and slot id; one a node no longer has is skipped. The engine never applied an
/// unresolved override, so removing one changes only the authored scene.
/// </remarks>
public sealed record RemoveMaterialSlots : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RemoveMaterialSlots"/> class.</summary>
    /// <param name="slots">The overrides to remove, by node.</param>
    public RemoveMaterialSlots(IReadOnlyDictionary<NodeId, IReadOnlyList<MaterialSlotTarget>> slots)
    {
        ArgumentNullException.ThrowIfNull(slots);
        this.Slots = slots.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<MaterialSlotTarget>)[.. pair.Value]);
    }

    /// <summary>Gets the overrides to remove, by node.</summary>
    public IReadOnlyDictionary<NodeId, IReadOnlyList<MaterialSlotTarget>> Slots { get; }

    /// <inheritdoc/>
    public string Label => "Clear Unresolved Material Slots";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Slots.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Slots.Count == 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No material slot to remove."));
        }

        return this.Slots.Keys.FirstOrDefault(id => GeometryOf(scope, id) is null) is { IsEmpty: false } missing
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no geometry component.", missing))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var removed = new Dictionary<NodeId, IReadOnlyList<RestoredMaterialSlot>>();
        foreach (var (id, targets) in this.Slots)
        {
            var geometry = GeometryOf(scope, id)!;
            var slots = geometry.OverrideSlots
                .Select((slot, index) => (Slot: slot as MaterialsSlot, Index: index))
                .Where(pair => pair.Slot is { } slot && targets.Any(target => SetMaterialSlot.SameSlot(slot.Target, target)))
                .Select(static pair => new RestoredMaterialSlot(new MaterialSlotAssignment(pair.Slot!.Target, pair.Slot.Material.Uri), pair.Index))
                .ToList();
            foreach (var slot in Enumerable.Reverse(slots))
            {
                geometry.OverrideSlots.RemoveAt(slot.Index);
            }

            if (slots.Count > 0)
            {
                removed[id] = slots;
            }
        }

        return removed.Count == 0
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new RestoreMaterialSlots(removed), [.. removed.Keys.Select(static id => new SceneChange.MaterialSlotsChanged(id))]);
    }

    private static GeometryComponent? GeometryOf(SceneEditScope scope, NodeId id)
        => scope.FindNode(id)?.Components.OfType<GeometryComponent>().FirstOrDefault();
}
