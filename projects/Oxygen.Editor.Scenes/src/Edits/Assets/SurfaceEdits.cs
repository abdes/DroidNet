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
    /// <param name="nodes">The nodes.</param>
    /// <param name="target">The surface, with the inventory revision it was chosen from.</param>
    /// <param name="property">A property of the surface part, such as <see cref="SceneSchema.Geometry.Surface.Material"/>.</param>
    /// <param name="value">The value, or <see langword="null"/> to reset to the geometry's.</param>
    /// <param name="label">The history label.</param>
    /// <param name="cancellationToken">Cancels the inventory read.</param>
    /// <returns>A <see cref="SetOverrides"/>, or why it cannot be built.</returns>
    public static async Task<SurfaceEdit> SetAsync(
        IGeometrySurfaceProvider inventories,
        ProjectContext project,
        IReadOnlyCollection<NodeId> nodes,
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

        if (inventory.GeometryUri != target.Geometry
            || !string.Equals(inventory.LayoutRevision, target.LayoutRevision, StringComparison.Ordinal)
            || inventory.Surfaces.All(surface => surface.SurfaceId != target.SurfaceId))
        {
            return new SurfaceEdit(Edit: null, SceneDiagnostic.Error(SceneEditCodes.SurfaceStale, "The surface changed since it was chosen. Refresh its inventory before editing."));
        }

        return new SurfaceEdit(new SetOverrides(nodes, [new OverrideWrite(property, target.Part, target.LayoutRevision, value) { PartOwner = target.Geometry }], label), Error: null);
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

    private static bool IsReadFailure(Exception error)
        => error is IOException or InvalidDataException or FormatException or InvalidOperationException or ArgumentException;

    private static SurfaceEdit Unavailable(string message)
        => new(Edit: null, SceneDiagnostic.Error(SceneEditCodes.SurfaceInventoryUnavailable, message));
}
