// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Owns identity-based material assignment, clearing and authored history.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc />
    public async Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        MaterialSlotTarget target,
        Uri? newMaterialUri,
        EditSessionToken session,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(session);
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new(Succeeded: false);
        }

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        var project = projectContexts.ActiveProject;
        if (project is null || project.ProjectId != context.Scene.Project.ProjectInfo.Id)
        {
            return this.SlotFailure(context, "The originating project is no longer active.");
        }

        if (!IsValidSlotRequest(target, newMaterialUri))
        {
            return this.SlotFailure(context, "Select an existing material slot and a valid material asset.");
        }

        var nodes = ResolveNodes(context.Scene, nodeIds);
        if (this.RejectLockedTargets(context, SceneOperationKinds.EditMaterialSlot, nodes) is { } lockFailure)
        {
            return lockFailure;
        }

        var geometries = nodes.Select(static node => node.Components.OfType<GeometryComponent>().FirstOrDefault()).ToArray();
        if (!SelectionOwnsGeometry(nodeIds, nodes, geometries, target))
        {
            return this.SlotFailure(context, "All selected nodes must still use the geometry that owns this slot.");
        }

        var (inventory, readFailure) = await this.ReadSlotInventoryAsync(context, project, target, cancellationToken).ConfigureAwait(true);
        if (readFailure is { } inventoryFailure)
        {
            return inventoryFailure;
        }

        cancellationToken.ThrowIfCancellationRequested();
        if (this.ValidateSlotInventory(context, project, nodes, geometries, target, inventory) is { } inventoryMismatch)
        {
            return inventoryMismatch;
        }

        return await this.ApplySlotEditAsync(context, nodes, geometries, target, newMaterialUri).ConfigureAwait(true);
    }

    private static bool IsValidSlotRequest(MaterialSlotTarget target, Uri? newMaterialUri)
    {
        var validTarget = target.SlotId != Guid.Empty
            && target.LayoutRevision is { Length: 64 }
            && target.LayoutRevision.All(Uri.IsHexDigit)
            && target.GeometryUri is { IsAbsoluteUri: true }
            && string.Equals(target.GeometryUri.Scheme, AssetUris.Scheme, StringComparison.Ordinal)
            && target.GeometryUri.Query.Length == 0
            && target.GeometryUri.Fragment.Length == 0;
        if (!validTarget)
        {
            return false;
        }

        return newMaterialUri is null
            || (newMaterialUri.IsAbsoluteUri
                && string.Equals(newMaterialUri.Scheme, AssetUris.Scheme, StringComparison.Ordinal)
                && !string.Equals(newMaterialUri.AbsolutePath, "/__uninitialized__", StringComparison.Ordinal)
                && newMaterialUri.Query.Length == 0
                && newMaterialUri.Fragment.Length == 0);
    }

    private static bool SelectionOwnsGeometry(IReadOnlyList<Guid> nodeIds, List<SceneNode> nodes, GeometryComponent?[] geometries, MaterialSlotTarget target)
        => nodeIds.Count != 0
            && nodeIds.Distinct().Count() == nodeIds.Count
            && nodes.Count == nodeIds.Count
            && !geometries.Any(geometry => geometry?.Geometry?.Uri != target.GeometryUri)
            && !nodes.Any(static node => node.Components.OfType<GeometryComponent>().Skip(1).Any());

    private static bool SelectionChanged(SceneDocumentCommandContext context, List<SceneNode> nodes, GeometryComponent?[] geometries, MaterialSlotTarget target)
        => nodes.Where((node, index) => !ReferenceEquals(FindNode(context.Scene, node.Id), node)
            || !ReferenceEquals(node.Components.OfType<GeometryComponent>().FirstOrDefault(), geometries[index])
            || node.Components.OfType<GeometryComponent>().Skip(1).Any()
            || geometries[index]!.Geometry?.Uri != target.GeometryUri).Any();

    private static bool InventoryMatches(GeometryMaterialSlotMetadata inventory, GeometryComponent?[] geometries, MaterialSlotTarget target)
        => inventory.GeometryUri == target.GeometryUri
            && string.Equals(inventory.LayoutRevision, target.LayoutRevision, StringComparison.Ordinal)
            && inventory.Slots.Any(slot => slot.SlotId == target.SlotId)
            && !geometries.Any(geometry => geometry!.OverrideSlots.OfType<MaterialsSlot>().Where(slot => SameSlot(slot.Target, target)).Skip(1).Any());

    private static bool SameSlot(MaterialSlotTarget left, MaterialSlotTarget right)
        => left.GeometryUri == right.GeometryUri && left.SlotId == right.SlotId;

    private static void ApplyMaterialSlotEdit(GeometryComponent geometry, MaterialSlotTarget target, Uri? materialUri)
    {
        var existing = geometry.OverrideSlots.OfType<MaterialsSlot>().SingleOrDefault(slot => SameSlot(slot.Target, target));
        if (materialUri is null)
        {
            if (existing is not null)
            {
                _ = geometry.OverrideSlots.Remove(existing);
            }

            return;
        }

        if (existing is null)
        {
            geometry.OverrideSlots.Add(new MaterialsSlot { Target = target, Material = new AssetReference<MaterialAsset>(materialUri) });
        }
        else
        {
            existing.Target = target;
            existing.Material = new AssetReference<MaterialAsset>(materialUri);
        }
    }

    private SceneCommandResult? ValidateSlotInventory(
        SceneDocumentCommandContext context,
        ProjectContext project,
        List<SceneNode> nodes,
        GeometryComponent?[] geometries,
        MaterialSlotTarget target,
        GeometryMaterialSlotMetadata? inventory)
    {
        if (!ReferenceEquals(projectContexts.ActiveProject, project) || SceneAuthoringGate.IsRetired(context.Scene) || SelectionChanged(context, nodes, geometries, target))
        {
            return this.SlotFailure(context, "The selection or geometry changed while its material slots were being read.");
        }

        if (inventory is null)
        {
            return this.SlotFailure(context, "Cook or refresh the geometry before editing its material slots.", SceneDiagnosticCodes.MaterialSlotInventoryUnavailable);
        }

        return InventoryMatches(inventory, geometries, target)
            ? null
            : this.SlotFailure(context, "The observed slot changed or has conflicting overrides. Refresh its inventory before editing.");
    }

    private async Task<(GeometryMaterialSlotMetadata? inventory, SceneCommandResult? failure)> ReadSlotInventoryAsync(
        SceneDocumentCommandContext context,
        ProjectContext project,
        MaterialSlotTarget target,
        CancellationToken cancellationToken)
    {
        try
        {
            return (await materialSlots.ReadAsync(project, target.GeometryUri, cancellationToken).ConfigureAwait(true), null);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return (null, new SceneCommandResult(Succeeded: false));
        }
        catch (Exception error) when (error is IOException or InvalidDataException or FormatException or InvalidOperationException or ArgumentException)
        {
            return (
                null,
                this.SlotFailure(
                    context,
                    "The native material-slot inventory could not be read. " + error.Message,
                    SceneDiagnosticCodes.MaterialSlotInventoryUnavailable));
        }
    }

    private async Task<SceneCommandResult> ApplySlotEditAsync(
        SceneDocumentCommandContext context,
        List<SceneNode> nodes,
        GeometryComponent?[] geometries,
        MaterialSlotTarget target,
        Uri? newMaterialUri)
    {
        var before = nodes.Select((node, index) => MaterialSlotState.Capture(node, geometries[index]!, target)).ToArray();
        foreach (var geometry in geometries)
        {
            ApplyMaterialSlotEdit(geometry!, target, newMaterialUri);
        }

        var after = nodes.Select((node, index) => MaterialSlotState.Capture(node, geometries[index]!, target)).ToArray();
        if (before.SequenceEqual(after))
        {
            return SceneCommandResult.Success;
        }

        context.History.AddChange("Restore Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, before, after).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var operation = await this.SyncEditedNodesAsync(
            context,
            nodes,
            SceneOperationKinds.EditMaterialSlot,
            node => this.sceneEngineSync.UpdateMaterialSlotAsync(context.Scene, node, target, newMaterialUri)).ConfigureAwait(true);
        return new(Succeeded: true, operation);
    }

    private SceneCommandResult SlotFailure(
        SceneDocumentCommandContext context,
        string message,
        string code = SceneDiagnosticCodes.MaterialSlotTargetInvalid)
        => this.ValidationFailure(SceneOperationKinds.EditMaterialSlot, code, "Material slot was not edited", message, context);

    private async Task ApplyMaterialSlotStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<MaterialSlotState> states, IReadOnlyList<MaterialSlotState> inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        // History keeps the references it captured; restore the assets' current identities after relocations.
        states = [.. states.Select(state => state with
        {
            Target = state.Target with { GeometryUri = this.RedirectCaptured(state.Target.GeometryUri)! },
            MaterialUri = this.RedirectCaptured(state.MaterialUri),
        })];
        if (states.Any(state => !ReferenceEquals(FindNode(context.Scene, state.Node.Id), state.Node)
            || !state.Node.Components.Contains(state.Geometry) || state.Geometry.Geometry?.Uri != state.Target.GeometryUri))
        {
            throw new InvalidOperationException("Material history no longer targets the same geometry components.");
        }

        this.WarnIfDeleted(context, states.Select(static state => state.MaterialUri));

        // History restores authored identity, including unresolved intent after reimport.
        // Native synchronization resolves surviving slot identity against the current inventory.
        foreach (var state in states)
        {
            ApplyMaterialSlotEdit(state.Geometry, state.Target, state.MaterialUri);
        }

        context.History.AddChange("Reapply Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = await this.SyncEditedNodesAsync(context, states.Select(static state => state.Node).ToList(), SceneOperationKinds.EditMaterialSlot, node =>
        {
            var state = states.Single(state => ReferenceEquals(state.Node, node));
            return this.sceneEngineSync.RestoreMaterialSlotAsync(context.Scene, node, state.Target, state.MaterialUri);
        }).ConfigureAwait(true);
    }

    private sealed record MaterialSlotState(SceneNode Node, GeometryComponent Geometry, MaterialSlotTarget Target, Uri? MaterialUri)
    {
        public static MaterialSlotState Capture(SceneNode node, GeometryComponent geometry, MaterialSlotTarget target)
        {
            var existing = geometry.OverrideSlots.OfType<MaterialsSlot>().SingleOrDefault(slot => SameSlot(slot.Target, target));
            return new(node, geometry, existing?.Target ?? target, existing?.Material.Uri);
        }
    }
}
