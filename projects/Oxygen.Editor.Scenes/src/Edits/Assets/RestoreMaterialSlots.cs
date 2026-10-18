// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Puts removed material overrides back where they were: the revert of <see cref="RemoveMaterialSlots"/>.</summary>
/// <remarks>
/// The overrides are restored as they were authored, even when no slot of the current geometry resolves them, so the
/// engine is not told about them.
/// </remarks>
public sealed record RestoreMaterialSlots : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RestoreMaterialSlots"/> class.</summary>
    /// <param name="slots">The overrides and their positions in the override list, by node, in ascending position.</param>
    public RestoreMaterialSlots(IReadOnlyDictionary<NodeId, IReadOnlyList<RestoredMaterialSlot>> slots)
    {
        ArgumentNullException.ThrowIfNull(slots);
        this.Slots = slots.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<RestoredMaterialSlot>)[.. pair.Value.OrderBy(static slot => slot.Index)]);
    }

    /// <summary>Gets the overrides and their positions, by node.</summary>
    public IReadOnlyDictionary<NodeId, IReadOnlyList<RestoredMaterialSlot>> Slots { get; }

    /// <inheritdoc/>
    public string Label => "Restore Unresolved Material Slots";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Slots.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        foreach (var (id, slots) in this.Slots)
        {
            if (GeometryOf(scope, id) is not { } geometry)
            {
                return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no geometry component.", id));
            }

            var count = geometry.OverrideSlots.Count;
            foreach (var slot in slots)
            {
                if (slot.Index < 0 || slot.Index > count++)
                {
                    return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The override's position is outside the override list.", id));
                }
            }
        }

        return this.Slots.Count == 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No material slot to restore."))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        foreach (var (id, slots) in this.Slots)
        {
            var geometry = GeometryOf(scope, id)!;
            foreach (var slot in slots)
            {
                var target = slot.Assignment.Target with { GeometryUri = scope.Resolve(slot.Assignment.Target.GeometryUri)! };

                // A removed override always named a material.
                var material = scope.Resolve(slot.Assignment.Material) ?? throw new InvalidOperationException("A restored material override names no material.");
                geometry.OverrideSlots.Insert(slot.Index, new MaterialsSlot { Target = target, Material = new AssetReference<MaterialAsset>(material) });
            }
        }

        foreach (var (id, slots) in this.Slots)
        {
            scope.WarnIfDeleted(slots.Select(slot => scope.Resolve(slot.Assignment.Material)), id);
        }
        var removals = this.Slots.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<MaterialSlotTarget>)[.. pair.Value.Select(static slot => slot.Assignment.Target)]);
        return new SceneEditApplication.Applied(new RemoveMaterialSlots(removals), [.. this.Slots.Keys.Select(static id => new SceneChange.MaterialSlotsChanged(id))]);
    }

    private static GeometryComponent? GeometryOf(SceneEditScope scope, NodeId id)
        => scope.FindNode(id)?.Components.OfType<GeometryComponent>().FirstOrDefault();
}
