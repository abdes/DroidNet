// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Sets the geometry asset of nodes' geometry components.</summary>
/// <remarks>
/// A geometry component always references a geometry: removing the component is how geometry is detached. Material
/// overrides stay as they are; those the new geometry does not have are removed by <see cref="RemoveMaterialSlots"/>.
/// </remarks>
public sealed record SetGeometry : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetGeometry"/> class.</summary>
    /// <param name="nodes">The nodes whose geometry components change.</param>
    /// <param name="geometry">The geometry asset.</param>
    public SetGeometry(IReadOnlyCollection<NodeId> nodes, Uri geometry)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        this.Geometries = nodes.Distinct().ToDictionary(static id => id, _ => geometry);
    }

    /// <summary>Initializes a new instance of the <see cref="SetGeometry"/> class, with one geometry per node.</summary>
    /// <param name="geometries">The geometry asset of each node.</param>
    [JsonConstructor]
    public SetGeometry(IReadOnlyDictionary<NodeId, Uri> geometries)
    {
        ArgumentNullException.ThrowIfNull(geometries);
        this.Geometries = geometries.ToDictionary();
    }

    /// <summary>Gets the geometry asset of each node.</summary>
    public IReadOnlyDictionary<NodeId, Uri> Geometries { get; }

    /// <inheritdoc/>
    public string Label => "Edit Geometry";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Geometries.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Geometries.Count == 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to edit."));
        }

        foreach (var (id, geometry) in this.Geometries)
        {
            if (GeometryOf(scope, id) is null)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no geometry component.", id));
            }

            // A deserialized edit can still carry null.
            if (geometry is null)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.GeometryRequired, "A geometry component must reference a geometry asset. Remove the component to detach geometry.", id));
            }

            if (AssetReferenceRules.Check([scope.Resolve(geometry)], AssetFileTypes.Geometry) is { } error)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, error, id));
            }
        }

        return SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = new Dictionary<NodeId, Uri>();
        foreach (var (id, captured) in this.Geometries)
        {
            var component = GeometryOf(scope, id)!;
            var geometry = scope.Resolve(captured);
            if (component.Geometry == geometry)
            {
                continue;
            }

            before[id] = component.Geometry;
            component.Geometry = geometry;
        }

        if (before.Count == 0)
        {
            return new SceneEditApplication.Applied(this, []);
        }

        foreach (var id in before.Keys)
        {
            scope.WarnIfDeleted([scope.Resolve(this.Geometries[id])], id);
        }

        return new SceneEditApplication.Applied(
            new SetGeometry(before),
            [.. before.Keys.Select(static id => new SceneChange.GeometryChanged(id))]);
    }

    private static GeometryComponent? GeometryOf(SceneEditScope scope, NodeId id)
        => scope.FindNode(id)?.Components.OfType<GeometryComponent>().FirstOrDefault();
}
