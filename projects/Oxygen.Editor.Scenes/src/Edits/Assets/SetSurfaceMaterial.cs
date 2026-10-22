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

/// <summary>Sets or clears the material override of one geometry surface on nodes that use that geometry.</summary>
/// <remarks>
/// The surface comes from the geometry's surface inventory, read before the edit (see
/// <see cref="SurfaceMaterialEdits"/>), which checks the surface against the current inventory. The edit checks that each node
/// still uses the surface's geometry. The surface is matched by geometry and surface id; an assignment replaces the node's override
/// of the surface, including the inventory revision it records, whichever inventory the override was chosen from.
/// </remarks>
public sealed record SetSurfaceMaterial : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetSurfaceMaterial"/> class.</summary>
    /// <param name="nodes">The nodes, all using the surface's geometry.</param>
    /// <param name="target">The surface.</param>
    /// <param name="material">The material, or <see langword="null"/> to clear the override.</param>
    public SetSurfaceMaterial(IReadOnlyCollection<NodeId> nodes, SurfaceTarget target, AssetPath? material)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(target);
        this.Assignments = nodes.Distinct().ToDictionary(static id => id, _ => new SurfaceMaterialAssignment(target, material));
    }

    /// <summary>Initializes a new instance of the <see cref="SetSurfaceMaterial"/> class, with one assignment per node.</summary>
    /// <param name="assignments">The assignment of each node, all for the same geometry surface.</param>
    [JsonConstructor]
    public SetSurfaceMaterial(IReadOnlyDictionary<NodeId, SurfaceMaterialAssignment> assignments)
    {
        ArgumentNullException.ThrowIfNull(assignments);
        this.Assignments = assignments.ToDictionary();
    }

    /// <summary>Gets the assignment of each node.</summary>
    public IReadOnlyDictionary<NodeId, SurfaceMaterialAssignment> Assignments { get; }

    /// <inheritdoc/>
    public string Label => "Edit Surface Material";

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
            if (geometries.Count != 1 || geometries[0].Geometry != assignment.Target.Geometry)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.SurfaceStale, "The node no longer uses the geometry that owns this surface.", id));
            }

            var overrides = geometries[0].OverrideSlots.OfType<MaterialsSlot>().Where(slot => SameSurface(slot.Target, assignment.Target)).ToList();
            if (overrides.Count > 1)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.SurfaceStale, "The surface has conflicting overrides; refresh its inventory before editing.", id));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = new Dictionary<NodeId, SurfaceMaterialAssignment>();
        foreach (var (id, captured) in this.Assignments)
        {
            var assignment = Resolve(scope, captured);
            var geometry = scope.FindNode(id)!.Components.OfType<GeometryComponent>().Single();
            var existing = geometry.OverrideSlots.OfType<MaterialsSlot>().SingleOrDefault(slot => SameSurface(slot.Target, assignment.Target));
            var old = new SurfaceMaterialAssignment(existing?.Target ?? assignment.Target, existing?.Material);
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

        return new SceneEditApplication.Applied(new SetSurfaceMaterial(before), [.. before.Keys.Select(static id => new SceneChange.SurfaceMaterialsChanged(id))]);
    }

    /// <summary>Tests whether two targets name the same surface of the same geometry, whatever their revisions.</summary>
    /// <param name="left">One target.</param>
    /// <param name="right">The other target.</param>
    /// <returns>Whether they name the same surface.</returns>
    internal static bool SameSurface(SurfaceTarget left, SurfaceTarget right)
        => left.Geometry == right.Geometry && left.SurfaceId == right.SurfaceId;

    private static SurfaceMaterialAssignment Resolve(SceneEditScope scope, SurfaceMaterialAssignment assignment)
        => new(assignment.Target with { Geometry = scope.Resolve(assignment.Target.Geometry) }, scope.Resolve(assignment.Material));

    private static void Assign(GeometryComponent geometry, MaterialsSlot? existing, SurfaceMaterialAssignment assignment)
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

    private static SceneDiagnostic? CheckTarget(SurfaceTarget target)
    {
        var valid = target.SurfaceId != Guid.Empty
            && target.LayoutRevision is { Length: 64 }
            && target.LayoutRevision.All(Uri.IsHexDigit)
            && target.Geometry is not null;
        return valid ? null : SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, "Select an existing surface of a geometry asset.");
    }

    private static SceneDiagnostic? CheckMaterial(AssetPath? material)
        => material is not null && AssetReferenceRules.Check([material], AssetFileTypes.Material) is { } error
            ? SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, error)
            : null;
}
