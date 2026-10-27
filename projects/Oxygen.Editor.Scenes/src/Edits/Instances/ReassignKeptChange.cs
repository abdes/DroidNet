// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Models;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Repairs a kept change where it was lost (scene instances 6.4, SI-16): points it at a node of the source that resolves.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="From">The kept change's node path.</param>
/// <param name="To">The path of the node of the source it now names.</param>
/// <remarks>
/// <para>
/// <see cref="KeptChanges.RepairOf"/> decides where. <b>In the model</b>, for a change made on the model the instance
/// places whose node its imports report as vanished: the model's import record gains a continuity entry, the chosen
/// node's anchor continuing the lost one, and the model is reimported; the chosen node then takes the lost node's ID, so
/// the change applies to it in every scene, and the scene's changes of the chosen node under its own ID become kept
/// changes, listed in a <see cref="SceneEditCodes.NodePathUnresolved"/> warning. The content pipeline writes the record
/// and reimports, as one operation of the project's writer. The record's previous bytes are kept: Undo has them written
/// back and the model reimported, refused with <see cref="SceneEditCodes.SourceChanged"/> when the record changed since. <b>In the instance</b>, for any other: the change moves to the chosen node's path, its values winning over
/// the node's own changes.
/// </para>
/// <para>
/// The source's complete structure is needed to tell the kept change and the chosen node: while it is not read, the edit
/// is refused with <see cref="SceneEditCodes.InstanceUnavailable"/>.
/// </para>
/// </remarks>
public sealed record ReassignKeptChange(NodeId Instance, NodePath From, NodePath To) : ISceneEdit, IEquatable<ReassignKeptChange>
{
    /// <summary>The history label.</summary>
    public const string HistoryLabel = "Reassign Kept Change";

    /// <inheritdoc/>
    public string Label => HistoryLabel;

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.From is null || this.To?.IsEmpty != false)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Reassigning a kept change names the change and the node it moves to.", this.Instance));
        }

        if (KeptChangeEdits.InstanceOf(scope, this.Instance) is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no kept changes.", this.Instance));
        }

        if (KeptChangeEdits.StructureOf(scope, this.Instance, instance, out var unavailable) is not { } structure)
        {
            return SceneEditValidation.Refused(unavailable!);
        }

        if (KeptChangeEdits.RefuseNotKept(this.Instance, instance, structure, this.From) is { } notKept)
        {
            return SceneEditValidation.Refused(notKept);
        }

        if (structure.Find(this.To) is null)
        {
            return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.NodePathUnresolved, $"{instance.Source.Name} holds no such node. Choose one of its nodes.", this.Instance));
        }

        var record = ReadRecord(scope, instance, out _);
        if (KeptChanges.RepairOf(instance, this.From, record) == KeptChangeRepair.Instance)
        {
            return SceneEditValidation.Valid;
        }

        var anchor = KeptChanges.PublishedAnchorOf(record, this.To);
        if (anchor is null)
        {
            return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.NodePathUnresolved, $"The last import of {instance.Source.Name} did not publish the chosen node. Reimport it first.", this.Instance));
        }

        return record!.NodeContinuity.Any(entry => string.Equals(entry.Anchor, anchor, StringComparison.Ordinal))
            ? SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, $"'{anchor}' already continues another node of {instance.Source.Name}. Choose another node.", this.Instance))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var instance = KeptChangeEdits.InstanceOf(scope, this.Instance)!;
        var structure = scope.StructureOf!.Invoke(instance.Source)!;
        var record = ReadRecord(scope, instance, out var previous);
        return KeptChanges.RepairOf(instance, this.From, record) == KeptChangeRepair.Model
            ? this.RepairModel(scope, instance, structure, record!, previous!)
            : this.RepairInstance(instance, structure);
    }

    // Reads the import record of an instance's model, when its source is one and the project has it.
    private static ModelImportSettings? ReadRecord(SceneEditScope scope, SceneInstanceComponent instance, out byte[]? bytes)
    {
        bytes = scope.ModelRecords?.Read(instance.Source);
        if (bytes is null)
        {
            return null;
        }

        try
        {
            return ModelImportSettings.Parse(bytes);
        }
        catch (Exception exception) when (exception is InvalidDataException or JsonException)
        {
            bytes = null;
            return null;
        }
    }

    // Overlays the kept change's values on the chosen node's own changes.
    private static InstancedNodeChanges Overlay(InstancedNodeChanges target, InstancedNodeChanges kept)
    {
        var merged = target;
        foreach (var values in kept.Components.Values)
        {
            merged = merged.With(values.All.Aggregate(merged.Of(values.ComponentKind), static (component, value) => component.With(value)));
        }

        foreach (var (slot, values) in kept.Parameters)
        {
            merged = values.Aggregate(merged, (changes, value) => changes.WithParameter(slot, value.Key, value.Value));
        }

        return merged;
    }

    private SceneEditApplication.Applied RepairInstance(SceneInstanceComponent instance, SceneStructure structure)
    {
        var before = instance.Changes;
        var moved = Overlay(before.Of(this.To), before.Of(this.From))
            .WithNames(KeptChanges.NamesOf(structure, this.To))
            .WithCarriedFrom(source: null);
        instance.Changes = before.With(this.From, InstancedNodeChanges.None).With(this.To, moved);
        return new SceneEditApplication.Applied(
            new RestoreInstanceChanges(this.Instance, HistoryLabel) { Changes = instance.Changes, Restored = before },
            [KeptChangeEdits.Changed(this.Instance, before, instance.Changes)]);
    }

    private SceneEditApplication.Applied RepairModel(SceneEditScope scope, SceneInstanceComponent instance, SceneStructure structure, ModelImportSettings record, byte[] previous)
    {
        var applied = record.WithContinuity(KeptChanges.PublishedAnchorOf(record, this.To)!, KeptChanges.LostAnchorOf(record, this.From)!).ToBytes();
        scope.ModelRecords!.Replace(instance.Source, previous, applied);

        // Once reimported, the chosen node has the lost node's ID: the change names it, by its names.
        var before = instance.Changes;
        instance.Changes = before.With(this.From, before.Of(this.From).WithNames(KeptChanges.NamesOf(structure, this.To)));
        var kept = KeptChanges.CountChanges(scope.Scene, instance.Source, this.To);
        if (kept > 0)
        {
            scope.Warn([SceneDiagnostic.Warning(
                SceneEditCodes.NodePathUnresolved,
                string.Create(CultureInfo.InvariantCulture, $"{kept} change(s) this scene makes of '{KeptChanges.NamesOf(structure, this.To)}' under its own ID become kept changes once {instance.Source.Name} is reimported."),
                this.Instance)]);
        }

        return new SceneEditApplication.Applied(
            new RestoreModelRecord(this.Instance, instance.Source, HistoryLabel)
            {
                Changes = instance.Changes,
                Restored = before,
                Applied = applied,
                Previous = previous,
            },
            [KeptChangeEdits.Changed(this.Instance, before, instance.Changes)]);
    }
}
