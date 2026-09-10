// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>Retains pending property values until acceptance or authoritative supersession.</summary>
internal sealed class PendingPropertySyncQueue
{
    private readonly Lock gate = new();
    private readonly Dictionary<Guid, SceneQueue> scenes = [];

    /// <summary>Installs an open-document lifetime, invalidating an earlier instance of the same scene.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="lifetime">The open-document lifetime.</param>
    public void Register(Guid sceneId, Guid lifetime)
    {
        lock (this.gate)
        {
            if (!this.scenes.TryGetValue(sceneId, out var scene) || scene.Lifetime != lifetime)
            {
                this.scenes[sceneId] = new(lifetime);
            }
        }
    }

    /// <summary>Invalidates pending and accepted boundaries for a closed lifetime.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="lifetime">The lifetime being closed.</param>
    public void Close(Guid sceneId, Guid lifetime)
    {
        lock (this.gate)
        {
            if (this.scenes.TryGetValue(sceneId, out var scene) && scene.Lifetime == lifetime)
            {
                _ = this.scenes.Remove(sceneId);
            }
        }
    }

    /// <summary>Stores immutable values, replacing only older work for the same fields.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="revision">The revision captured before asynchronous work.</param>
    /// <param name="nodeId">The edited node.</param>
    /// <param name="entries">The requested property values.</param>
    /// <returns>The remaining pending request count.</returns>
    public int Enqueue(Guid sceneId, SceneSyncRevision revision, Guid nodeId, IReadOnlyList<EnginePropertyValueEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        lock (this.gate)
        {
            if (!this.scenes.TryGetValue(sceneId, out var scene) || scene.Lifetime != revision.DocumentLifetime)
            {
                return this.Count(sceneId);
            }

            var current = entries.Where(entry => IsNewer(scene, revision, nodeId, entry)).ToArray();
            if (current.Length == 0)
            {
                return scene.Pending.Count;
            }

            foreach (var entry in current)
            {
                scene.Latest[new(nodeId, entry.Component, entry.FieldId)] = revision;
            }

            RemoveSupersededFields(scene);
            var request = new PendingPropertySyncEntry(Guid.NewGuid(), revision, nodeId, Array.AsReadOnly(current));
            scene.Pending.Add(request.RequestId, request);
            return scene.Pending.Count;
        }
    }

    /// <summary>Returns replay work without removing unacknowledged requests.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="lifetime">The currently projected document lifetime.</param>
    /// <returns>Pending requests in authoring and preview order.</returns>
    public IReadOnlyList<PendingPropertySyncEntry> Snapshot(Guid sceneId, Guid lifetime)
    {
        lock (this.gate)
        {
            return this.scenes.TryGetValue(sceneId, out var scene) && scene.Lifetime == lifetime
                ? scene.Pending.Values.OrderBy(entry => entry.Revision.Revision).ThenBy(entry => entry.Revision.Sequence).ToArray()
                : [];
        }
    }

    /// <summary>Checks the latest payload immediately before replay.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="request">The request selected for replay.</param>
    /// <returns>The current payload, or null if superseded or closed.</returns>
    public PendingPropertySyncEntry? Current(Guid sceneId, PendingPropertySyncEntry request)
    {
        lock (this.gate)
        {
            return this.scenes.TryGetValue(sceneId, out var scene) && scene.Lifetime == request.Revision.DocumentLifetime
                ? scene.Pending.GetValueOrDefault(request.RequestId)
                : null;
        }
    }

    /// <summary>Removes only the successfully applied or deleted-target request.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="request">The acknowledged request.</param>
    public void Acknowledge(Guid sceneId, PendingPropertySyncEntry request)
    {
        lock (this.gate)
        {
            if (this.scenes.TryGetValue(sceneId, out var scene) && scene.Lifetime == request.Revision.DocumentLifetime)
            {
                _ = scene.Pending.Remove(request.RequestId);
            }
        }
    }

    /// <summary>Keeps failed replay visible and available for retry.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="request">The attempted request.</param>
    /// <param name="failure">The unsuccessful replay outcome.</param>
    public void Fail(Guid sceneId, PendingPropertySyncEntry request, SyncOutcome failure)
    {
        lock (this.gate)
        {
            if (this.scenes.TryGetValue(sceneId, out var scene) && scene.Lifetime == request.Revision.DocumentLifetime
                && scene.Pending.TryGetValue(request.RequestId, out var current))
            {
                scene.Pending[request.RequestId] = current with { Failure = failure };
            }
        }
    }

    /// <summary>Acknowledges the boundary of a fully accepted coherent scene snapshot.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="boundary">The captured snapshot boundary.</param>
    public void Supersede(Guid sceneId, SceneSyncRevision boundary)
    {
        lock (this.gate)
        {
            if (!this.scenes.TryGetValue(sceneId, out var scene) || scene.Lifetime != boundary.DocumentLifetime)
            {
                return;
            }

            if (scene.Boundary is { } accepted && boundary.IsAtOrBefore(accepted))
            {
                return;
            }

            scene.Boundary = boundary;
            foreach (var pending in scene.Pending.Values.Where(entry => entry.Revision.IsAtOrBefore(boundary)).ToArray())
            {
                _ = scene.Pending.Remove(pending.RequestId);
            }
        }
    }

    /// <summary>Supersedes older fields after a complete node or component publication.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <param name="nodeId">The published node.</param>
    /// <param name="component">The complete component, or null for the node.</param>
    /// <param name="revision">The accepted publication revision.</param>
    public void SupersedeTarget(Guid sceneId, Guid nodeId, EngineComponentId? component, SceneSyncRevision revision)
    {
        lock (this.gate)
        {
            if (!this.scenes.TryGetValue(sceneId, out var scene) || scene.Lifetime != revision.DocumentLifetime)
            {
                return;
            }

            var key = (nodeId, component);
            if (!scene.TargetBoundaries.TryGetValue(key, out var previous) || !revision.IsAtOrBefore(previous))
            {
                scene.TargetBoundaries[key] = revision;
                RemoveSupersededFields(scene);
            }
        }
    }

    /// <summary>Gets the number of unacknowledged requests for a scene.</summary>
    /// <param name="sceneId">The authored scene identity.</param>
    /// <returns>The pending request count.</returns>
    public int Count(Guid sceneId)
    {
        lock (this.gate)
        {
            return this.scenes.TryGetValue(sceneId, out var scene) ? scene.Pending.Count : 0;
        }
    }

    private static bool IsNewer(SceneQueue scene, SceneSyncRevision revision, Guid nodeId, EnginePropertyValueEntry entry)
        => (scene.Boundary is not { } boundary || !revision.IsAtOrBefore(boundary))
            && !IsTargetCovered(scene, revision, nodeId, entry.Component)
            && (!scene.Latest.TryGetValue(new(nodeId, entry.Component, entry.FieldId), out var latest) || !revision.IsAtOrBefore(latest));

    private static bool IsCurrent(SceneQueue scene, SceneSyncRevision revision, Guid nodeId, EnginePropertyValueEntry entry)
        => scene.Latest.TryGetValue(new(nodeId, entry.Component, entry.FieldId), out var latest) && latest == revision
            && !IsTargetCovered(scene, revision, nodeId, entry.Component);

    private static bool IsTargetCovered(SceneQueue scene, SceneSyncRevision revision, Guid nodeId, EngineComponentId component)
        => (scene.TargetBoundaries.TryGetValue((nodeId, null), out var node) && revision.IsAtOrBefore(node))
            || (scene.TargetBoundaries.TryGetValue((nodeId, component), out var target) && revision.IsAtOrBefore(target));

    private static void RemoveSupersededFields(SceneQueue scene)
    {
        foreach (var (requestId, pending) in scene.Pending.ToArray())
        {
            var remaining = pending.Entries.Where(entry => IsCurrent(scene, pending.Revision, pending.NodeId, entry)).ToArray();
            if (remaining.Length == 0)
            {
                _ = scene.Pending.Remove(requestId);
            }
            else if (remaining.Length != pending.Entries.Count)
            {
                scene.Pending[requestId] = pending with { Entries = Array.AsReadOnly(remaining) };
            }
        }
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Auto)]
    private readonly record struct FieldKey(Guid NodeId, EngineComponentId Component, ushort FieldId);

    private sealed class SceneQueue(Guid lifetime)
    {
        public Guid Lifetime { get; } = lifetime;

        public SceneSyncRevision? Boundary { get; set; }

        public Dictionary<Guid, PendingPropertySyncEntry> Pending { get; } = [];

        public Dictionary<FieldKey, SceneSyncRevision> Latest { get; } = [];

        public Dictionary<(Guid nodeId, EngineComponentId? component), SceneSyncRevision> TargetBoundaries { get; } = [];
    }
}
