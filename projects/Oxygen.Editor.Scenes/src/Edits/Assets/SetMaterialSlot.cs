// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Sets or clears the material override of one geometry slot on nodes that use that geometry.</summary>
/// <remarks>
/// The slot comes from the geometry's material slot inventory, read before the edit (see
/// <see cref="MaterialSlotEdits"/>), which checks the slot against the current inventory. The edit checks that each node
/// still uses the slot's geometry. The slot is matched by geometry and slot id; an assignment replaces the node's override
/// of the slot, including the inventory revision it records, whichever inventory the override was chosen from.
/// </remarks>
public sealed record SetMaterialSlot : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetMaterialSlot"/> class.</summary>
    /// <param name="nodes">The nodes, all using the slot's geometry.</param>
    /// <param name="target">The slot.</param>
    /// <param name="material">The material, or <see langword="null"/> to clear the override.</param>
    public SetMaterialSlot(IReadOnlyCollection<NodeId> nodes, MaterialSlotTarget target, Uri? material)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(target);
        this.Assignments = nodes.Distinct().ToDictionary(static id => id, _ => new MaterialSlotAssignment(target, material));
    }

    /// <summary>Initializes a new instance of the <see cref="SetMaterialSlot"/> class, with one assignment per node.</summary>
    /// <param name="assignments">The assignment of each node, all for the same geometry slot.</param>
    [JsonConstructor]
    public SetMaterialSlot(IReadOnlyDictionary<NodeId, MaterialSlotAssignment> assignments)
    {
        ArgumentNullException.ThrowIfNull(assignments);
        this.Assignments = assignments.ToDictionary();
    }

    /// <summary>Gets the assignment of each node.</summary>
    public IReadOnlyDictionary<NodeId, MaterialSlotAssignment> Assignments { get; }

    /// <inheritdoc/>
    public string Label => "Edit Material Slot";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Assignments.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Assignments.Count == 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to edit."));
        }

        foreach (var (id, captured) in this.Assignments)
        {
            var assignment = Resolve(scope, captured);
            if ((CheckTarget(assignment.Target) ?? CheckMaterial(assignment.Material)) is { } invalid)
            {
                return SceneEditValidation.Invalid(invalid with { Node = id });
            }

            var geometries = scope.FindNode(id)?.Components.OfType<GeometryComponent>().ToList() ?? [];
            if (geometries.Count != 1 || geometries[0].Geometry != assignment.Target.GeometryUri)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.MaterialSlotStale, "The node no longer uses the geometry that owns this slot.", id));
            }

            var overrides = geometries[0].OverrideSlots.OfType<MaterialsSlot>().Where(slot => SameSlot(slot.Target, assignment.Target)).ToList();
            if (overrides.Count > 1)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.MaterialSlotStale, "The slot has conflicting overrides; refresh its inventory before editing.", id));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = new Dictionary<NodeId, MaterialSlotAssignment>();
        foreach (var (id, captured) in this.Assignments)
        {
            var assignment = Resolve(scope, captured);
            var geometry = scope.FindNode(id)!.Components.OfType<GeometryComponent>().Single();
            var existing = geometry.OverrideSlots.OfType<MaterialsSlot>().SingleOrDefault(slot => SameSlot(slot.Target, assignment.Target));
            var old = new MaterialSlotAssignment(existing?.Target ?? assignment.Target, existing?.Material);
            if (old == assignment || (old.Material is null && assignment.Material is null))
            {
                continue;
            }

            before[id] = old;
            Assign(geometry, existing, assignment);
        }

        if (before.Count == 0)
        {
            return new SceneEditApplication.Applied(this, []);
        }

        foreach (var id in before.Keys)
        {
            scope.WarnIfDeleted([Resolve(scope, this.Assignments[id]).Material], id);
        }
        return new SceneEditApplication.Applied(new SetMaterialSlot(before), [.. before.Keys.Select(static id => new SceneChange.MaterialSlotsChanged(id))]);
    }

    /// <summary>Tests whether two targets name the same slot of the same geometry, whatever their revisions.</summary>
    /// <param name="left">One target.</param>
    /// <param name="right">The other target.</param>
    /// <returns>Whether they name the same slot.</returns>
    internal static bool SameSlot(MaterialSlotTarget left, MaterialSlotTarget right)
        => left.GeometryUri == right.GeometryUri && left.SlotId == right.SlotId;

    private static MaterialSlotAssignment Resolve(SceneEditScope scope, MaterialSlotAssignment assignment)
        => new(assignment.Target with { GeometryUri = scope.Resolve(assignment.Target.GeometryUri) }, scope.Resolve(assignment.Material));

    private static void Assign(GeometryComponent geometry, MaterialsSlot? existing, MaterialSlotAssignment assignment)
    {
        if (assignment.Material is null)
        {
            _ = geometry.OverrideSlots.Remove(existing!);
        }
        else if (existing is null)
        {
            geometry.OverrideSlots.Add(new MaterialsSlot { Target = assignment.Target, Material = assignment.Material });
        }
        else
        {
            existing.Target = assignment.Target;
            existing.Material = assignment.Material;
        }
    }

    private static SceneDiagnostic? CheckTarget(MaterialSlotTarget target)
    {
        var valid = target.SlotId != Guid.Empty
            && target.LayoutRevision is { Length: 64 }
            && target.LayoutRevision.All(Uri.IsHexDigit)
            && AssetUris.IsAssetUri(target.GeometryUri)
            && target.GeometryUri.Query.Length == 0
            && target.GeometryUri.Fragment.Length == 0;
        return valid ? null : SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, "Select an existing material slot of a geometry asset.");
    }

    private static SceneDiagnostic? CheckMaterial(Uri? material)
        => material is not null && AssetReferenceRules.Check([material], AssetFileTypes.Material) is { } error
            ? SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, error)
            : null;
}
