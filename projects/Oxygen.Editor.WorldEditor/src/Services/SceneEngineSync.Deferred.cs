// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Preserves non-scalar mutations that arrive while a frozen scene is being projected.</summary>
public sealed partial class SceneEngineSync
{
    private static void TrackNativeHierarchy(SceneProjection projection, RuntimeWorldCommand command)
    {
        if (command is RuntimeReparentSceneNode reparent)
        {
            projection.NativeParents[reparent.Child] = reparent.Parent;
        }
        else if (command is RuntimeReparentSceneNodes reparentMany)
        {
            foreach (var child in reparentMany.Children)
            {
                projection.NativeParents[child] = reparentMany.Parent;
            }
        }

        HashSet<Guid> removed = command switch
        {
            RuntimeRemoveSceneNode remove => [remove.NodeId],
            RuntimeRemoveSceneNodes remove => [.. remove.Nodes],
            _ => [],
        };
        var previousCount = -1;
        while (previousCount != removed.Count)
        {
            previousCount = removed.Count;
            foreach (var (node, parent) in projection.NativeParents)
            {
                if (parent is { } parentId && removed.Contains(parentId))
                {
                    _ = removed.Add(node);
                }
            }
        }

        foreach (var node in removed)
        {
            _ = projection.NativeParents.Remove(node);
        }
    }

    private static RuntimeWorldCommand? FilterDeferredCommand(Scene scene, RuntimeWorldCommand command)
        => command switch
        {
            RuntimeCreateNode creation when FindNode(scene, creation.NodeId) is null => null,
            RuntimeSetLocalTransform transform when FindNode(scene, transform.NodeId) is null => null,
            RuntimeAttachPerspectiveCamera camera when FindNode(scene, camera.NodeId)?.Components.Any(component => component is PerspectiveCamera) != true => null,
            RuntimeAttachDirectionalLight light when FindNode(scene, light.NodeId)?.Components.Any(component => component is DirectionalLightComponent) != true => null,
            RuntimeAttachPointLight light when FindNode(scene, light.NodeId)?.Components.Any(component => component is PointLightComponent) != true => null,
            RuntimeAttachSpotLight light when FindNode(scene, light.NodeId)?.Components.Any(component => component is SpotLightComponent) != true => null,
            RuntimeRemoveSceneNode remove when FindNode(scene, remove.NodeId) is not null => null,
            RuntimeRemoveSceneNodes remove => remove with { Nodes = remove.Nodes.Where(id => FindNode(scene, id) is null).ToImmutableArray() },
            RuntimeReparentSceneNode reparent when FindNode(scene, reparent.Child) is null => null,
            RuntimeDetachGeometry detach when FindNode(scene, detach.NodeId)?.Components.Any(component => component is GeometryComponent) == true => null,
            RuntimeDetachLight detach when FindNode(scene, detach.NodeId)?.Components.Any(component => component is LightComponent) == true => null,
            RuntimeDetachCamera detach when FindNode(scene, detach.NodeId)?.Components.Any(component => component is CameraComponent) == true => null,
            RuntimeSetGeometry geometry when FindNode(scene, geometry.NodeId)?.Components.Any(component => component is GeometryComponent) != true => null,
            RuntimeSetMaterialOverride material when FindNode(scene, material.NodeId)?.Components.Any(component => component is GeometryComponent) != true => null,
            _ => command,
        };

    private bool TryDeferNodeCreation(SceneNode node, Guid? parentId, out Task completionTask)
    {
        lock (this.documentGate)
        {
            if (this.projecting is not { } projection || !ReferenceEquals(projection.Lifetime.Scene, node.Scene))
            {
                completionTask = Task.CompletedTask;
                return false;
            }

            var snapshotScene = new Scene(node.Scene.Project) { Id = node.Scene.Id, Name = node.Scene.Name };
            snapshotScene.SetEnvironment(node.Scene.Environment);
            var snapshot = SceneNode.CreateAndHydrate(snapshotScene, node.Dehydrate());
            var commands = new List<RuntimeWorldCommand> { new RuntimeCreateNode(snapshot.Name, node.Id, parentId, InitializeWorldAsRoot: false) };
            var collector = this.activeWorld! with { Collect = commands.Add };
            ApplyTransform(collector, snapshot);
            var captured = this.ApplyRenderableComponents(collector, snapshot);
            var revision = this.CaptureRevision(node.Scene);
            var completion = new TaskCompletionSource<SyncOutcome>(TaskCreationOptions.RunContinuationsAsynchronously);
            var scope = Scope(node.Scene, node) with { DocumentId = projection.Lifetime.Metadata.DocumentId, DocumentLifetime = revision.DocumentLifetime, AuthoringRevision = revision.Revision };
            var failure = captured ? null : Failed(SceneOperationKinds.NodeCreate, scope, LiveSyncDiagnosticCodes.MutationFailed, "A component could not be prepared for runtime publication.");
            var request = new PendingWorldMutation(Guid.NewGuid(), revision, commands.ToImmutableArray(), SceneOperationKinds.NodeCreate, scope, failure, completion, CaptureFailed: !captured);
            projection.Lifetime.PendingMutations.Add(request.RequestId, request);
            this.OnPendingPropertySyncCountChanged(node.Scene.Id, this.GetPendingPropertySyncCount(node.Scene.Id));
            completionTask = completion.Task;
            return true;
        }
    }

    private bool TryDeferMutation(Scene scene, WorldDispatch world, Action<WorldDispatch> apply, string operationKind, AffectedScope scope, out SyncOutcome outcome)
    {
        lock (this.documentGate)
        {
            if (this.projecting is not { } projection || !ReferenceEquals(projection.Lifetime.Scene, scene))
            {
                outcome = null!;
                return false;
            }

            var revision = this.CaptureRevision(scene);
            if (revision.DocumentLifetime == Guid.Empty)
            {
                outcome = Cancelled(operationKind, scope);
                return true;
            }

            var commands = new List<RuntimeWorldCommand>();
            apply(world with { Collect = commands.Add });
            if (commands.Count == 0)
            {
                outcome = Accepted(operationKind, scope);
                return true;
            }

            scope = scope with { DocumentId = projection.Lifetime.Metadata.DocumentId, DocumentLifetime = revision.DocumentLifetime, AuthoringRevision = revision.Revision };
            var request = new PendingWorldMutation(Guid.NewGuid(), revision, commands.ToImmutableArray(), operationKind, scope);
            projection.Lifetime.PendingMutations.Add(request.RequestId, request);
            outcome = RuntimeWorldUnavailable(operationKind, scope) with { Message = "The scene mutation will replay after the captured scene snapshot." };
            this.OnPendingPropertySyncCountChanged(scene.Id, this.GetPendingPropertySyncCount(scene.Id));
            return true;
        }
    }

    private void ExecuteOrDefer(Scene scene, WorldDispatch world, RuntimeWorldCommand command)
    {
        if (!this.TryDeferMutation(scene, world, target => target.Execute(command), "Scene.Mutation", Scope(scene), out _))
        {
            world.Execute(command);
        }
    }

    private async Task ReplayWorldMutationAsync(SceneProjection projection, PendingWorldMutation request, WorldDispatch world)
    {
        var lifetime = projection.Lifetime;
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(lifetime) || !lifetime.PendingMutations.ContainsKey(request.RequestId))
            {
                return;
            }
        }

        if (request.CaptureFailed)
        {
            this.RecordMutationFailure(lifetime, request, world, request.Failure!);
            return;
        }

        try
        {
            foreach (var command in request.Commands)
            {
                await this.ApplyDeferredCommandAsync(projection, request, world, command).ConfigureAwait(false);
            }

            lock (this.documentGate)
            {
                _ = lifetime.PendingMutations.Remove(request.RequestId);
            }

            _ = request.Completion?.TrySetResult(Accepted(request.OperationKind, request.Scope));
        }
        catch (RuntimeDispatchException exception)
        {
            var outcome = FromRuntimeOutcome(exception.Result, request.OperationKind, request.Scope, LiveSyncDiagnosticCodes.MutationRejected, LiveSyncDiagnosticCodes.MutationFailed);
            this.RecordMutationFailure(lifetime, request, world, outcome);
        }
        catch (Exception exception) when (EngineInteropExceptionPolicy.IsRecoverable(exception))
        {
            this.RecordMutationFailure(lifetime, request, world, Failed(request.OperationKind, request.Scope, LiveSyncDiagnosticCodes.MutationFailed, exception.Message, exception));
        }
    }

    private async Task ApplyDeferredCommandAsync(SceneProjection projection, PendingWorldMutation request, WorldDispatch world, RuntimeWorldCommand command)
    {
        RuntimeCreateNode creation;
        Task<RuntimeCommandResult> pending;
        lock (this.documentGate)
        {
            this.EnsureDeferredTarget(projection, request, world);
            if (FilterDeferredCommand(projection.Lifetime.Scene, command) is not { } current)
            {
                return;
            }

            if (current is not RuntimeCreateNode create)
            {
                world.Execute(current);
                TrackNativeHierarchy(projection, current);
                this.SupersedeMutationProperties(projection.Lifetime.Scene, current, request.Revision);
                return;
            }

            if (projection.NativeParents.ContainsKey(create.NodeId))
            {
                return;
            }

            creation = create;
            pending = world.Commands.CreateNodeAsync(new(request.RequestId, world.Target, create), world.CancellationToken);
        }

        var result = await pending.ConfigureAwait(false);
        lock (this.documentGate)
        {
            this.EnsureDeferredTarget(projection, request, world);
            if (!result.Succeeded)
            {
                throw new RuntimeDispatchException(result);
            }

            projection.NativeParents[creation.NodeId] = creation.ParentId;
            this.SupersedeMutationProperties(projection.Lifetime.Scene, creation, request.Revision);
        }
    }

    private void EnsureDeferredTarget(SceneProjection projection, PendingWorldMutation request, WorldDispatch world)
    {
        if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != world.Target)
        {
            throw new RuntimeDispatchException(new RuntimeCommandResult(request.RequestId, world.Target.RunId, RuntimeCommandStatus.Cancelled, "The deferred scene target was invalidated."));
        }
    }

    private void RecordMutationFailure(DocumentLifetime lifetime, PendingWorldMutation request, WorldDispatch world, SyncOutcome outcome)
    {
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(lifetime) || this.activeWorld?.Target != world.Target)
            {
                _ = request.Completion?.TrySetResult(Cancelled(request.OperationKind, request.Scope));
                return;
            }

            lifetime.PendingMutations[request.RequestId] = request with { Failure = outcome };
            _ = request.Completion?.TrySetResult(outcome);
            this.PublishReplayFailures(request.RequestId, [outcome]);
        }
    }

    private void FinishDeferredWaiters(DocumentLifetime lifetime)
    {
        lock (this.documentGate)
        {
            foreach (var pending in lifetime.PendingMutations.Values)
            {
                _ = pending.Completion?.TrySetResult(pending.Failure ?? RuntimeWorldUnavailable(pending.OperationKind, pending.Scope));
            }
        }
    }

    private void SupersedeMutationProperties(Scene scene, RuntimeWorldCommand command, SceneSyncRevision revision)
    {
        switch (command)
        {
            case RuntimeCreateNode value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, component: null, revision);
                break;
            case RuntimeRemoveSceneNode value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, component: null, revision);
                break;
            case RuntimeRemoveSceneNodes value:
                foreach (var node in value.Nodes)
                {
                    this.pendingPropertySyncs.SupersedeTarget(scene.Id, node, component: null, revision);
                }

                break;
            case RuntimeSetLocalTransform value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, EngineComponentId.Transform, revision);
                break;
            case RuntimeAttachPerspectiveCamera value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, EngineComponentId.PerspectiveCamera, revision);
                break;
            case RuntimeDetachCamera value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, EngineComponentId.PerspectiveCamera, revision);
                break;
            case RuntimeAttachDirectionalLight value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, EngineComponentId.DirectionalLight, revision);
                break;
            case RuntimeDetachLight value:
                this.pendingPropertySyncs.SupersedeTarget(scene.Id, value.NodeId, EngineComponentId.DirectionalLight, revision);
                break;
        }
    }

    private sealed record PendingWorldMutation(
        Guid RequestId,
        SceneSyncRevision Revision,
        ImmutableArray<RuntimeWorldCommand> Commands,
        string OperationKind,
        AffectedScope Scope,
        SyncOutcome? Failure = null,
        TaskCompletionSource<SyncOutcome>? Completion = null,
        bool CaptureFailed = false);
}
