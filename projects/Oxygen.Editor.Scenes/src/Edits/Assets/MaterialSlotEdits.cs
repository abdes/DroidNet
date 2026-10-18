// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>Builds material slot edits from the geometries' material slot inventories, read before the edit.</summary>
/// <remarks>
/// Call it on the scene's thread: it reads the nodes' geometries before reading the inventories. The edit it returns
/// checks again, when it runs, that each node still uses the slot's geometry.
/// </remarks>
public static class MaterialSlotEdits
{
    /// <summary>Builds the edit that sets or clears one slot's material on nodes using the slot's geometry.</summary>
    /// <param name="inventories">Reads material slot inventories.</param>
    /// <param name="project">The scene's project.</param>
    /// <param name="nodes">The nodes.</param>
    /// <param name="target">The slot, with the inventory revision it was chosen from.</param>
    /// <param name="material">The material, or <see langword="null"/> to clear the override.</param>
    /// <param name="cancellationToken">Cancels the inventory read.</param>
    /// <returns>A <see cref="SetMaterialSlot"/>, or why it cannot be built.</returns>
    public static async Task<MaterialSlotEdit> SetAsync(
        IGeometryMaterialSlotProvider inventories,
        ProjectContext project,
        IReadOnlyCollection<NodeId> nodes,
        MaterialSlotTarget target,
        AssetPath? material,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(inventories);
        ArgumentNullException.ThrowIfNull(target);
        GeometryMaterialSlotMetadata? inventory;
        try
        {
            inventory = await inventories.ReadAsync(project, target.Geometry, cancellationToken).ConfigureAwait(true);
        }
        catch (Exception error) when (IsReadFailure(error))
        {
            return Unavailable("The material slot inventory could not be read. " + error.Message);
        }

        if (inventory is null)
        {
            return Unavailable("Cook or refresh the geometry before editing its material slots.");
        }

        return inventory.GeometryUri == target.Geometry
            && string.Equals(inventory.LayoutRevision, target.LayoutRevision, StringComparison.Ordinal)
            && inventory.Slots.Any(slot => slot.SlotId == target.SlotId)
            ? new MaterialSlotEdit(new SetMaterialSlot(nodes, target, material), Error: null)
            : new MaterialSlotEdit(Edit: null, SceneDiagnostic.Error(SceneEditCodes.MaterialSlotStale, "The slot changed since it was chosen. Refresh its inventory before editing."));
    }

    /// <summary>Builds the edit that removes the material overrides no slot of the nodes' current geometries resolves.</summary>
    /// <remarks>
    /// An override for another geometry is unresolved. One for the current geometry is unresolved when its inventory no
    /// longer has the slot; when the inventory cannot be read, such overrides stay.
    /// </remarks>
    /// <param name="inventories">Reads material slot inventories.</param>
    /// <param name="project">The scene's project.</param>
    /// <param name="scene">The scene.</param>
    /// <param name="nodes">The nodes; missing ids and nodes without geometry are skipped.</param>
    /// <param name="cancellationToken">Cancels the inventory reads.</param>
    /// <returns>A <see cref="RemoveMaterialSlots"/>, or no edit when every override resolves.</returns>
    public static async Task<MaterialSlotEdit> ClearUnresolvedAsync(
        IGeometryMaterialSlotProvider inventories,
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
            .Select(static pair => (pair.Id, Uri: pair.Geometry!.Geometry, Slots: pair.Geometry.OverrideSlots.OfType<MaterialsSlot>().Select(static slot => slot.Target).ToList()))
            .ToList();
        var read = new Dictionary<AssetPath, GeometryMaterialSlotMetadata>();
        foreach (var uri in geometries.Select(static pair => pair.Uri).Distinct())
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
                // Same-geometry overrides stay until an inventory proves them unresolved.
            }
        }

        var unresolved = geometries
            .Select(pair => (pair.Id, Slots: pair.Slots.Where(target => target.Geometry != pair.Uri
                || (read.GetValueOrDefault(pair.Uri) is { } inventory && inventory.Slots.All(slot => slot.SlotId != target.SlotId))).ToArray()))
            .Where(static pair => pair.Slots.Length > 0)
            .ToDictionary(static pair => pair.Id, static pair => (IReadOnlyList<MaterialSlotTarget>)pair.Slots);
        return new MaterialSlotEdit(unresolved.Count == 0 ? null : new RemoveMaterialSlots(unresolved), Error: null);
    }

    private static bool IsReadFailure(Exception error)
        => error is IOException or InvalidDataException or FormatException or InvalidOperationException or ArgumentException;

    private static MaterialSlotEdit Unavailable(string message)
        => new(Edit: null, SceneDiagnostic.Error(SceneEditCodes.MaterialSlotInventoryUnavailable, message));
}
