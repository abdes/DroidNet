// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Removes material overrides from nodes' geometry components, such as the overrides no surface resolves anymore.</summary>
/// <remarks>
/// Overrides are matched by geometry and surface id; one a node no longer has is skipped. The engine never applied an
/// unresolved override, so removing one changes only the authored scene.
/// </remarks>
public sealed record RemoveSurfaceMaterials : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RemoveSurfaceMaterials"/> class.</summary>
    /// <param name="surfaces">The overrides to remove, by node.</param>
    public RemoveSurfaceMaterials(IReadOnlyDictionary<NodeId, IReadOnlyList<SurfaceTarget>> surfaces)
    {
        ArgumentNullException.ThrowIfNull(surfaces);
        this.Surfaces = surfaces.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<SurfaceTarget>)[.. pair.Value]);
    }

    /// <summary>Gets the overrides to remove, by node.</summary>
    public IReadOnlyDictionary<NodeId, IReadOnlyList<SurfaceTarget>> Surfaces { get; }

    /// <inheritdoc/>
    public string Label => "Clear Unresolved Surface Materials";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Surfaces.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Surfaces.Count == 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No surface material to remove."));
        }

        return this.Surfaces.Keys.FirstOrDefault(id => GeometryOf(scope, id) is null) is { IsEmpty: false } missing
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no geometry component.", missing))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var removed = new Dictionary<NodeId, IReadOnlyList<RestoredSurfaceMaterial>>();
        foreach (var (id, targets) in this.Surfaces)
        {
            var geometry = GeometryOf(scope, id)!;
            var overridesRemoved = geometry.OverrideSlots
                .Select((slot, index) => (Slot: slot as MaterialsSlot, Index: index))
                .Where(pair => pair.Slot is { } slot && targets.Any(target => SetSurfaceMaterial.SameSurface(slot.Target, target)))
                .Select(static pair => new RestoredSurfaceMaterial(new SurfaceMaterialAssignment(pair.Slot!.Target, pair.Slot.Material), pair.Index))
                .ToList();
            foreach (var slot in Enumerable.Reverse(overridesRemoved))
            {
                geometry.OverrideSlots.RemoveAt(slot.Index);
            }

            if (overridesRemoved.Count > 0)
            {
                removed[id] = overridesRemoved;
            }
        }

        return removed.Count == 0
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new RestoreSurfaceMaterials(removed), [.. removed.Keys.Select(static id => new SceneChange.SurfaceMaterialsChanged(id))]);
    }

    private static GeometryComponent? GeometryOf(SceneEditScope scope, NodeId id)
        => scope.FindNode(id)?.Components.OfType<GeometryComponent>().FirstOrDefault();
}
