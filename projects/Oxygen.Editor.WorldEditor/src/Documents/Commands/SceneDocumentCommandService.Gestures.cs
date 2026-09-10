// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.TimeMachine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Uses the shared snapshot controller for component and environment gestures.</summary>
public sealed partial class SceneDocumentCommandService
{
    private readonly CommitGroupController propertyCommitGroups = new();
    private readonly Dictionary<Guid, PropertyGesture> propertyGestures = [];

    /// <inheritdoc/>
    public Task<SceneCommandResult> EditPropertiesForTargetsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyDictionary<Guid, PropertyEdit> edits,
        string label,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(edits);
        ArgumentNullException.ThrowIfNull(session);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        var phase = session.State;
        var snapshot = new PropertySnapshot(edits);
        return this.ApplyPropertyGestureAsync(context, snapshot, label, session, phase);
    }

    private static IReadOnlyDictionary<PropertyId, PropertyDescriptor>? GestureDescriptors(string kind)
        => kind switch
        {
            TransformKind => Transform.ById,
            PerspectiveCameraKind => PerspectiveCamera.ById,
            DirectionalLightKind => DirectionalLight.ById,
            SceneEnvironmentKind => SceneEnvironment.ById,
            _ => null,
        };

    private static bool GestureMatches(
        PropertyGesture active,
        SceneDocumentCommandContext context,
        string kind,
        PropertySnapshot requested,
        IReadOnlyCollection<PropertyId> properties,
        IReadOnlyDictionary<Guid, object> models)
        => ReferenceEquals(active.Context.Scene, context.Scene)
            && ReferenceEquals(active.Context.Metadata, context.Metadata)
            && active.Context.DocumentId == context.DocumentId
            && string.Equals(active.Kind, kind, StringComparison.Ordinal)
            && active.Nodes.ToHashSet().SetEquals(requested.Nodes)
            && active.Descriptors.Select(value => value.Id).ToHashSet().SetEquals(properties)
            && (string.Equals(kind, SceneEnvironmentKind, StringComparison.Ordinal)
                || models.All(pair => active.Targets.TryGetValue(pair.Key, out var original) && ReferenceEquals(original, pair.Value)));

    private static Dictionary<Guid, object> GestureTargets(SceneDocumentCommandContext context, string kind, IEnumerable<Guid> ids)
    {
        if (string.Equals(kind, SceneEnvironmentKind, StringComparison.Ordinal))
        {
            return new() { [context.Scene.Id] = new SceneEnvironmentPropertyTarget(context.Scene.Environment) };
        }

        var targets = new Dictionary<Guid, object>();
        foreach (var node in ResolveNodes(context.Scene, ids.ToArray()))
        {
            object? target = kind switch
            {
                TransformKind => node.Components.OfType<TransformComponent>().FirstOrDefault(),
                PerspectiveCameraKind => node.Components.OfType<PerspectiveCamera>().FirstOrDefault(),
                DirectionalLightKind => node.Components.OfType<DirectionalLightComponent>().FirstOrDefault(),
                _ => null,
            };
            if (target is not null)
            {
                targets[node.Id] = target;
            }
        }

        return targets;
    }

    private static void ApplyGestureSnapshot(
        SceneDocumentCommandContext context,
        string kind,
        PropertySnapshot snapshot,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors,
        IReadOnlyDictionary<Guid, object>? originalTargets = null)
    {
        var targets = originalTargets ?? GestureTargets(context, kind, snapshot.Nodes);
        foreach (var (id, edit) in snapshot.PerNode)
        {
            if (targets.TryGetValue(id, out var target))
            {
                PropertyApply.ApplyToTarget(target, edit, descriptors);
            }
        }

        if (string.Equals(kind, SceneEnvironmentKind, StringComparison.Ordinal) && targets[context.Scene.Id] is SceneEnvironmentPropertyTarget environment)
        {
            context.Scene.SetEnvironment(environment.Value);
            ApplyEnvironmentSunBinding(context.Scene, environment.Value.SunNodeId);
        }
    }

    private Task<SceneCommandResult> ApplyPropertyGestureAsync(
        SceneDocumentCommandContext context,
        PropertySnapshot requested,
        string label,
        EditSessionToken token,
        EditSessionState phase)
    {
        _ = this.propertyGestures.TryGetValue(token.SessionId, out var active);
        if (!token.IsOneShot && phase != EditSessionState.Open)
        {
            return active is null
                ? Task.FromResult(SceneCommandResult.Success)
                : this.EndPropertyGestureAsync(context, active, phase);
        }

        if (requested.PerNode.Values.All(edit => edit.Count == 0) || phase == EditSessionState.Cancelled)
        {
            return Task.FromResult(SceneCommandResult.Success);
        }

        var ids = requested.PerNode.Values.SelectMany(edit => edit.Ids).Distinct().ToArray();
        var kinds = ids.Select(id => id.ComponentKind).Distinct(StringComparer.Ordinal).ToArray();
        if (kinds.Length != 1 || GestureDescriptors(kinds[0]) is not { } descriptors)
        {
            return Task.FromResult(this.ValidationFailure(SceneOperationKinds.EditTransform, "PROPERTY_GESTURE_KIND", "Property edit rejected", "The gesture must target one supported component kind.", context));
        }

        var kind = kinds[0];
        var models = GestureTargets(context, kind, requested.Nodes);
        if (!models.Keys.ToHashSet().SetEquals(requested.Nodes))
        {
            return Task.FromResult(this.ValidationFailure(OperationKindForPropertyKind(kind), "PROPERTY_TARGET_GONE", "Property edit rejected", "An edited component no longer exists.", context));
        }

        if (this.ValidateGesture(context, kind, requested) is { } validation)
        {
            return Task.FromResult(validation);
        }

        if (active is not null && !GestureMatches(active, context, kind, requested, ids, models))
        {
            return Task.FromResult(this.ValidationFailure(OperationKindForPropertyKind(kind), "PROPERTY_GESTURE_TARGET_CHANGED", "Property edit rejected", "The gesture's document, target set, or property set changed.", context));
        }

        var touched = ids.Select(id => descriptors[id]).ToArray();
        var before = PropertySnapshot.Capture(models, touched);
        if (active is null)
        {
            var key = token.IsOneShot ? Guid.NewGuid().ToString("N", CultureInfo.InvariantCulture) : token.SessionId.ToString("N", CultureInfo.InvariantCulture);
            var group = this.propertyCommitGroups.Begin(key, requested.Nodes.ToArray(), before, label);
            active = new(context, token, kind, group.Key, group.Nodes, touched, before, label, models);
            if (!token.IsOneShot)
            {
                this.propertyGestures[token.SessionId] = active;
            }
        }

        ApplyGestureSnapshot(context, kind, requested, descriptors);
        var after = PropertySnapshot.Capture(GestureTargets(context, kind, active.Nodes), active.Descriptors);
        this.propertyCommitGroups.RecordPreview(active.Key, after);
        return phase == EditSessionState.Open
            ? this.PreviewPropertyGestureAsync(active, after)
            : this.EndPropertyGestureAsync(context, active, EditSessionState.Committed);
    }

    private SceneCommandResult? ValidateGesture(SceneDocumentCommandContext context, string kind, PropertySnapshot requested)
    {
        foreach (var (nodeId, edit) in requested.PerNode)
        {
            SceneCommandResult? schemaFailure;
            ValidationIssue? domain;
            if (string.Equals(kind, SceneEnvironmentKind, StringComparison.Ordinal))
            {
                var conversion = this.TryBuildSceneEnvironmentEdit(context, edit);
                schemaFailure = conversion.Result;
                domain = schemaFailure is null ? ValidateEnvironmentEdit(context.Scene, conversion.Edit) : null;
            }
            else
            {
                schemaFailure = this.ValidateComponentPropertyEdit(context, edit, kind);
                domain = schemaFailure is not null ? null : kind switch
                {
                    PerspectiveCameraKind => ValidatePerspectiveCameraEdit(context.Scene, [nodeId], BuildPerspectiveCameraEditFromPropertyEdit(edit)),
                    DirectionalLightKind => ValidateDirectionalLightEdit(BuildDirectionalLightEditFromPropertyEdit(edit)),
                    _ => null,
                };
            }

            if (schemaFailure is not null)
            {
                return schemaFailure;
            }

            if (domain is { IsFailure: true } failure)
            {
                return this.ValidationFailure(OperationKindForPropertyKind(kind), failure.Code, failure.Title, failure.Message, context);
            }
        }

        return null;
    }

    private async Task<SceneCommandResult> PreviewPropertyGestureAsync(PropertyGesture gesture, PropertySnapshot after)
    {
        var revision = this.sceneEngineSync.CaptureRevision(gesture.Context.Scene, gesture.Context.Metadata);
        var pending = this.SynchronizeGestureSnapshotAsync(gesture, after, EditSessionState.Open, revision, gesture.Context.Scene.Environment);
        _ = gesture.Pending.RemoveAll(task => task.IsCompleted);
        gesture.Pending.Add(pending);
        var operation = await pending.ConfigureAwait(true);
        return new(Succeeded: true, operation);
    }

    private async Task<SceneCommandResult> EndPropertyGestureAsync(SceneDocumentCommandContext context, PropertyGesture gesture, EditSessionState phase)
    {
        if (!ReferenceEquals(context.Scene, gesture.Context.Scene)
            || !ReferenceEquals(context.Metadata, gesture.Context.Metadata)
            || context.DocumentId != gesture.Context.DocumentId)
        {
            return this.ValidationFailure(OperationKindForPropertyKind(gesture.Kind), "PROPERTY_GESTURE_DOCUMENT_CHANGED", "Property edit rejected", "The gesture belongs to another document lifetime.", context);
        }

        _ = this.propertyGestures.Remove(gesture.Token.SessionId);
        var models = GestureTargets(context, gesture.Kind, gesture.Nodes);
        var targetsChanged = !string.Equals(gesture.Kind, SceneEnvironmentKind, StringComparison.Ordinal)
            && (models.Count != gesture.Targets.Count
                || models.Any(pair => !gesture.Targets.TryGetValue(pair.Key, out var original) || !ReferenceEquals(original, pair.Value)));
        if (targetsChanged)
        {
            phase = EditSessionState.Cancelled;
        }

        var after = PropertySnapshot.Capture(models, gesture.Descriptors);
        _ = this.propertyCommitGroups.Close(gesture.Key, after);
        var op = new PropertyOp(gesture.Nodes, gesture.Before, after, gesture.Label);
        var metadataUpdate = Task.CompletedTask;
        var revision = this.sceneEngineSync.CaptureRevision(context.Scene, context.Metadata);
        if (phase == EditSessionState.Cancelled)
        {
            gesture.Token.Cancel();
            ApplyGestureSnapshot(context, gesture.Kind, gesture.Before, GestureDescriptors(gesture.Kind)!, gesture.Targets);
            after = gesture.Before;
        }
        else
        {
            gesture.Token.Commit();
            if (op.EffectiveEdit().Count > 0)
            {
                this.RegisterGestureHistory(context, gesture.Kind, op);
                metadataUpdate = this.MarkDirtyAsync(context, out revision);
            }
        }

        var environment = context.Scene.Environment;
        var operation = await CompletePublicationAsync(metadataUpdate, this.SynchronizeGestureSnapshotAsync(gesture, after, phase, revision, environment)).ConfigureAwait(true);
        _ = await Task.WhenAll(gesture.Pending).ConfigureAwait(true);
        return targetsChanged
            ? this.ValidationFailure(OperationKindForPropertyKind(gesture.Kind), "PROPERTY_TARGET_GONE", "Property edit cancelled", "An edited component was removed or replaced.", context)
            : new(Succeeded: true, operation);
    }

    private void RegisterGestureHistory(SceneDocumentCommandContext context, string kind, PropertyOp op)
        => context.History.AddChange(op.Label, async () =>
        {
            ApplyGestureSnapshot(context, kind, op.Before, GestureDescriptors(kind)!);
            this.RegisterGestureHistory(context, kind, op.Inverse());
            var gesture = new PropertyGesture(context, EditSessionToken.OneShot, kind, string.Empty, op.Nodes, [], op.Before, op.Label, GestureTargets(context, kind, op.Nodes));
            var environment = context.Scene.Environment;
            var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
            _ = await CompletePublicationAsync(metadataUpdate, this.SynchronizeGestureSnapshotAsync(gesture, op.Before, EditSessionState.Committed, revision, environment)).ConfigureAwait(true);
        });

    private async Task<Guid?> SynchronizeGestureSnapshotAsync(
        PropertyGesture gesture,
        PropertySnapshot snapshot,
        EditSessionState phase,
        SceneSyncRevision revision,
        SceneEnvironmentData environment)
    {
        Guid? firstResult = null;
        foreach (var (nodeId, edit) in snapshot.PerNode)
        {
            var scene = gesture.Context.Scene;
            Func<CancellationToken, Task<SyncOutcome>> sync;
            if (string.Equals(gesture.Kind, SceneEnvironmentKind, StringComparison.Ordinal))
            {
                sync = async cancellationToken =>
                {
                    var result = await this.sceneEngineSync.UpdateEnvironmentAsync(scene, environment, revision, cancellationToken).ConfigureAwait(true);
                    return result.PerField.Values.FirstOrDefault(value => value.Status != SyncStatus.Accepted)
                        ?? new SyncOutcome(result.Overall, SceneOperationKinds.EditEnvironment, Scope(gesture.Context));
                };
            }
            else if (FindNode(scene, nodeId) is { } node)
            {
                var currentTargets = GestureTargets(gesture.Context, gesture.Kind, [nodeId]);
                if (!currentTargets.TryGetValue(nodeId, out var currentTarget)
                    || !gesture.Targets.TryGetValue(nodeId, out var originalTarget)
                    || !ReferenceEquals(currentTarget, originalTarget))
                {
                    continue;
                }

                IReadOnlyList<EnginePropertyValueEntry> entries = gesture.Kind switch
                {
                    TransformKind => BuildTransformPropertyEntries(edit),
                    PerspectiveCameraKind => BuildPerspectiveCameraPropertyEntries(BuildPerspectiveCameraEditFromPropertyEdit(edit)),
                    DirectionalLightKind => BuildDirectionalLightPropertyEntries(BuildDirectionalLightEditFromPropertyEdit(edit)),
                    _ => [],
                };
                sync = cancellationToken => this.sceneEngineSync.UpdatePropertiesAsync(scene, node, entries, revision, cancellationToken);
            }
            else
            {
                continue;
            }

            var outcome = phase switch
            {
                EditSessionState.Open => await this.sceneEngineSync.TryPreviewSyncAsync(scene.Id, nodeId, DateTimeOffset.UtcNow, sync).ConfigureAwait(true),
                EditSessionState.Cancelled => await this.sceneEngineSync.CancelPreviewSyncAsync(scene.Id, nodeId, sync).ConfigureAwait(true),
                _ => await this.sceneEngineSync.CompleteTerminalSyncAsync(scene.Id, nodeId, sync).ConfigureAwait(true),
            };
            if (outcome is not null)
            {
                var published = await this.PublishSyncOutcomeAsync(gesture.Context, OperationKindForPropertyKind(gesture.Kind), outcome).ConfigureAwait(true);
                firstResult ??= published;
            }
        }

        return firstResult;
    }

    private sealed class PropertyGesture(
        SceneDocumentCommandContext context, EditSessionToken token, string kind, string key,
        IReadOnlyList<Guid> nodes, IReadOnlyList<PropertyDescriptor> descriptors, PropertySnapshot before, string label,
        IReadOnlyDictionary<Guid, object> targets)
    {
        public SceneDocumentCommandContext Context { get; } = context;

        public EditSessionToken Token { get; } = token;

        public string Kind { get; } = kind;

        public string Key { get; } = key;

        public string Label { get; } = label;

        public IReadOnlyList<Guid> Nodes { get; } = nodes;

        public IReadOnlyList<PropertyDescriptor> Descriptors { get; } = descriptors;

        public PropertySnapshot Before { get; } = before;

        public IReadOnlyDictionary<Guid, object> Targets { get; } = targets;

        public List<Task<Guid?>> Pending { get; } = [];
    }
}
