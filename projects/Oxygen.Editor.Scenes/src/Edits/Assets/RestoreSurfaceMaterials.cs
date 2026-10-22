// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Puts removed material overrides back where they were: the revert of <see cref="RemoveSurfaceMaterials"/>.</summary>
/// <remarks>
/// The overrides are restored as they were authored, even when no entry of the current geometry resolves them, so the
/// engine is not told about them.
/// </remarks>
public sealed record RestoreSurfaceMaterials : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RestoreSurfaceMaterials"/> class.</summary>
    /// <param name="surfaces">The overrides and their positions in the override list, by node, in ascending position.</param>
    public RestoreSurfaceMaterials(IReadOnlyDictionary<NodeId, IReadOnlyList<RestoredSurfaceMaterial>> surfaces)
    {
        ArgumentNullException.ThrowIfNull(surfaces);
        this.Surfaces = surfaces.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<RestoredSurfaceMaterial>)[.. pair.Value.OrderBy(static entry => entry.Index)]);
    }

    /// <summary>Gets the overrides and their positions, by node.</summary>
    public IReadOnlyDictionary<NodeId, IReadOnlyList<RestoredSurfaceMaterial>> Surfaces { get; }

    /// <inheritdoc/>
    public string Label => "Restore Unresolved Surface Materials";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Surfaces.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        foreach (var (id, restored) in this.Surfaces)
        {
            if (GeometryOf(scope, id) is not { } geometry)
            {
                return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no geometry component.", id));
            }

            var count = geometry.OverrideSlots.Count;
            foreach (var entry in restored)
            {
                if (entry.Index < 0 || entry.Index > count++)
                {
                    return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The override's position is outside the override list.", id));
                }
            }
        }

        return this.Surfaces.Count == 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No material entry to restore."))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        foreach (var (id, restored) in this.Surfaces)
        {
            var geometry = GeometryOf(scope, id)!;
            foreach (var entry in restored)
            {
                var target = entry.Assignment.Target with { Geometry = scope.Resolve(entry.Assignment.Target.Geometry) };

                // A removed override always named a material.
                var material = scope.Resolve(entry.Assignment.Material) ?? throw new InvalidOperationException("A restored material override names no material.");
                geometry.OverrideSlots.Insert(entry.Index, new MaterialsSlot { Target = target, Material = material });
            }
        }

        foreach (var (id, restored) in this.Surfaces)
        {
            scope.WarnIfDeleted(restored.Select(entry => scope.Resolve(entry.Assignment.Material)), id);
        }

        var removals = this.Surfaces.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<SurfaceTarget>)[.. pair.Value.Select(static entry => entry.Assignment.Target)]);
        return new SceneEditApplication.Applied(new RemoveSurfaceMaterials(removals), [.. this.Surfaces.Keys.Select(static id => new SceneChange.SurfaceMaterialsChanged(id))]);
    }

    private static GeometryComponent? GeometryOf(SceneEditScope scope, NodeId id)
        => scope.FindNode(id)?.Components.OfType<GeometryComponent>().FirstOrDefault();
}
