// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Builds the overrides edits of geometry surfaces from the geometries' surface inventories, read before the edit.</summary>
/// <remarks>
/// Call it on the scene's thread: it reads the nodes' geometries before reading the inventories. An edit is built only for a
/// surface the current inventory has; the edit itself checks the override's shape and value when it runs.
/// </remarks>
public static class SurfaceEdits
{
    /// <summary>Builds the edit that sets or resets one property of a surface on nodes using the surface's geometry.</summary>
    /// <param name="inventories">Reads surface inventories.</param>
    /// <param name="project">The scene's project.</param>
    /// <param name="nodes">The nodes; a node an instance brings in takes the value as a change of its instance root.</param>
    /// <param name="target">The surface, with the inventory revision it was chosen from.</param>
    /// <param name="property">A property of the surface part, such as <see cref="SceneSchema.Geometry.Surface.Material"/>.</param>
    /// <param name="value">The value, or <see langword="null"/> to reset to the geometry's.</param>
    /// <param name="label">The history label.</param>
    /// <param name="cancellationToken">Cancels the inventory read.</param>
    /// <returns>A <see cref="SetOverrides"/>, or why it cannot be built.</returns>
    public static async Task<SurfaceEdit> SetAsync(
        IGeometrySurfaceProvider inventories,
        ProjectContext project,
        IReadOnlyCollection<NodeReference> nodes,
        SurfaceTarget target,
        PropertyId property,
        object? value,
        string label,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(inventories);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(property);
        if (await CheckTargetAsync(inventories, project, target, cancellationToken).ConfigureAwait(true) is { } error)
        {
            return new SurfaceEdit(Edit: null, error);
        }

        OverrideWrite[] writes = [new OverrideWrite(property, target.Part, target.LayoutRevision, value) { PartOwner = target.Geometry }];
        return new SurfaceEdit(NodeOverrides.Write(nodes.Distinct().ToDictionary(static node => node, _ => (IReadOnlyList<OverrideWrite>)writes), label), Error: null);
    }

    /// <summary>Builds the edit that resets every override of one surface on nodes, so the geometry's values apply.</summary>
    /// <remarks>
    /// A reset needs no inventory: it removes overrides whatever surfaces the geometry has, such as the overrides kept for a
    /// surface lost on reimport. Nodes without an override of the surface are left as they are.
    /// </remarks>
    /// <param name="nodes">The nodes; a node an instance brings in loses its instance root's changes of the surface.</param>
    /// <param name="part">The surface.</param>
    /// <param name="label">The history label.</param>
    /// <returns>A <see cref="SetOverrides"/> that resets each property of the surface part.</returns>
    public static SetOverrides Reset(IReadOnlyCollection<NodeReference> nodes, PartId part, string label)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        OverrideWrite[] writes = [.. SceneSchema.Overridable.Keys
            .Where(property => string.Equals(property.PartKind, part.Kind, StringComparison.Ordinal))
            .Select(property => new OverrideWrite(property, part, LayoutRevision: null, Value: null))];
        return NodeOverrides.Write(nodes.Distinct().ToDictionary(static node => node, _ => (IReadOnlyList<OverrideWrite>)writes), label);
    }

    /// <summary>
    /// Builds the edit that moves the overrides nodes keep for a surface their geometry no longer has onto a surface it has.
    /// </summary>
    /// <remarks>
    /// The author chooses the surface: a matching name or material does not make two surfaces the same. Each node's kept
    /// values replace its values of the chosen surface, and the kept surface's overrides are removed, in one undoable edit.
    /// </remarks>
    /// <param name="inventories">Reads surface inventories.</param>
    /// <param name="project">The scene's project.</param>
    /// <param name="scene">The scene.</param>
    /// <param name="nodes">The nodes; missing ids and nodes without overrides of <paramref name="kept"/> are skipped.</param>
    /// <param name="kept">The surface the overrides were made for.</param>
    /// <param name="target">The surface of the current inventory that takes them.</param>
    /// <param name="cancellationToken">Cancels the inventory read.</param>
    /// <returns>A <see cref="SetOverrides"/>, no edit when no node keeps a change, or why it cannot be built.</returns>
    public static async Task<SurfaceEdit> RetargetAsync(
        IGeometrySurfaceProvider inventories,
        ProjectContext project,
        Scene scene,
        IReadOnlyCollection<NodeId> nodes,
        PartId kept,
        SurfaceTarget target,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(inventories);
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(target);
        if (await CheckTargetAsync(inventories, project, target, cancellationToken).ConfigureAwait(true) is { } error)
        {
            return new SurfaceEdit(Edit: null, error);
        }

        var writes = new Dictionary<NodeId, IReadOnlyList<OverrideWrite>>();
        foreach (var id in nodes.Distinct())
        {
            var values = scene.AllNodes.FirstOrDefault(node => node.Id == id)?.Components.OfType<GeometryComponent>().FirstOrDefault()?.Overrides.All
                .Where(entry => entry.Part == kept)
                .ToList() ?? [];
            if (values.Count > 0 && kept != target.Part)
            {
                writes[id] = [
                    .. values.Select(static entry => new OverrideWrite(entry.Property, entry.Part, LayoutRevision: null, Value: null)),
                    .. values.Select(entry => new OverrideWrite(entry.Property, target.Part, target.LayoutRevision, entry.Value) { PartOwner = target.Geometry }),
                ];
            }
        }

        return new SurfaceEdit(writes.Count == 0 ? null : new SetOverrides(writes, "Apply Kept Surface Changes"), Error: null);
    }

    /// <summary>Builds the edit that resets the overrides of surfaces the nodes' current geometries do not have.</summary>
    /// <remarks>
    /// An override of a surface the current inventory lacks is unresolved, such as one kept from a geometry the node used
    /// before. When an inventory cannot be read, the overrides of its nodes stay.
    /// </remarks>
    /// <param name="inventories">Reads surface inventories.</param>
    /// <param name="project">The scene's project.</param>
    /// <param name="scene">The scene.</param>
    /// <param name="nodes">The nodes; missing ids and nodes without geometry are skipped.</param>
    /// <param name="cancellationToken">Cancels the inventory reads.</param>
    /// <returns>A <see cref="SetOverrides"/>, or no edit when every override resolves.</returns>
    public static async Task<SurfaceEdit> ClearUnresolvedAsync(
        IGeometrySurfaceProvider inventories,
        ProjectContext project,
        Scene scene,
        IReadOnlyCollection<NodeId> nodes,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(inventories);
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(nodes);
        var geometries = nodes.Distinct()
            .Select(id => (Id: id, Geometry: scene.AllNodes.FirstOrDefault(node => node.Id == id)?.Components.OfType<GeometryComponent>().FirstOrDefault()))
            .Where(static pair => pair.Geometry is not null)
            .Select(static pair => (pair.Id, Component: pair.Geometry!))
            .ToList();
        var read = new Dictionary<AssetPath, GeometrySurfaceMetadata>();
        foreach (var uri in geometries.Select(static pair => pair.Component.Geometry).Distinct())
        {
            try
            {
                if (await inventories.ReadAsync(project, uri, cancellationToken).ConfigureAwait(true) is { } inventory)
                {
                    read[uri] = inventory;
                }
            }
            catch (Exception error) when (IsReadFailure(error))
            {
                // Overrides stay until an inventory proves them unresolved.
            }
        }

        var resets = new Dictionary<NodeId, IReadOnlyList<OverrideWrite>>();
        foreach (var (id, component) in geometries)
        {
            if (read.GetValueOrDefault(component.Geometry) is not { } inventory)
            {
                continue;
            }

            OverrideWrite[] writes = [.. component.Overrides.All
                .Where(entry => entry.Part is { Kind: SceneSchema.Geometry.Surface.Kind } part && inventory.Surfaces.All(surface => surface.SurfaceId != part.Id))
                .Select(static entry => new OverrideWrite(entry.Property, entry.Part, LayoutRevision: null, Value: null))];
            if (writes.Length > 0)
            {
                resets[id] = writes;
            }
        }

        return new SurfaceEdit(resets.Count == 0 ? null : new SetOverrides(resets, "Clear Unresolved Surface Overrides"), Error: null);
    }

    // Why the surface cannot be edited now: its inventory is unreadable, or no longer lists the surface as it was chosen.
    private static async Task<SceneDiagnostic?> CheckTargetAsync(
        IGeometrySurfaceProvider inventories,
        ProjectContext project,
        SurfaceTarget target,
        CancellationToken cancellationToken)
    {
        GeometrySurfaceMetadata? inventory;
        try
        {
            inventory = await inventories.ReadAsync(project, target.Geometry, cancellationToken).ConfigureAwait(true);
        }
        catch (Exception error) when (IsReadFailure(error))
        {
            return Unavailable("The surface inventory could not be read. " + error.Message);
        }

        if (inventory is null)
        {
            return Unavailable("Cook or refresh the geometry before editing its surfaces.");
        }

        return inventory.GeometryUri != target.Geometry
            || !string.Equals(inventory.LayoutRevision, target.LayoutRevision, StringComparison.Ordinal)
            || inventory.Surfaces.All(surface => surface.SurfaceId != target.SurfaceId)
                ? SceneDiagnostic.Error(SceneEditCodes.SurfaceStale, "The surface changed since it was chosen. Refresh its inventory before editing.")
                : null;
    }

    private static bool IsReadFailure(Exception error)
        => error is IOException or InvalidDataException or FormatException or InvalidOperationException or ArgumentException;

    private static SceneDiagnostic Unavailable(string message)
        => SceneDiagnostic.Error(SceneEditCodes.SurfaceInventoryUnavailable, message);
}
