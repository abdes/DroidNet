// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Projects scene instances: the engine builds each instance from its source and the scene's changes.</summary>
/// <remarks>
/// The engine keeps each instance's request and builds it again when the mounted content changes, so a source that is
/// not cooked yet, or is published again, shows without the projector's help. The projector sends what the scene
/// changes: an instance added or given another source is built again, a change set or cleared is sent alone, and an
/// instance removed is detached.
/// </remarks>
internal sealed partial class ScenePreview
{
    /// <summary>Gets the commands that bring the engine's instance from the changes it was sent to the current ones.</summary>
    /// <param name="node">The instance root.</param>
    /// <param name="previous">The changes the engine was sent.</param>
    /// <param name="current">The changes the instance holds now.</param>
    /// <returns>A clear per change that went away, then a set per change that is new or holds another value.</returns>
    internal static List<RuntimeWorldCommand> ChangeCommands(SceneNode node, InstanceChanges previous, InstanceChanges current)
    {
        var before = Flatten(previous);
        var after = Flatten(current);
        List<RuntimeWorldCommand> commands =
        [
            .. before.Keys.Where(key => !after.ContainsKey(key))
                .Select(key => new RuntimeClearInstanceChange(node.Id, key.Path, ChangeIdOf(key.Property), key.Part)),
            .. after.Where(pair => !before.TryGetValue(pair.Key, out var old) || !Equals(old.Value, pair.Value.Value))
                .Select(pair => new RuntimeSetInstanceChange(node.Id, ToRuntimeChange(pair.Key, pair.Value))),
        ];
        return commands;
    }

    private static void RecordInstances(DocumentLifetime lifetime, IEnumerable<SceneNode> nodes)
    {
        foreach (var node in nodes)
        {
            if (InstanceOf(node) is { } instance)
            {
                lifetime.Instances[node.Id] = new ProjectedInstance(instance.Source, instance.Changes);
            }
            else
            {
                _ = lifetime.Instances.Remove(node.Id);
            }
        }
    }

    private static SceneInstanceComponent? InstanceOf(SceneNode node)
        => node.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    private static string InstanceSourcePath(AssetPath source)
        => AssetFileTypes.SceneProductOf(source) is { } scene
            ? ContentPipelinePaths.ToCookedScenePath(scene)
            : throw new ArgumentException($"'{source}' is neither a model nor a scene.", nameof(source));

    // Every property change by node path, property and part. Script parameter changes have no live effect: Live Preview
    // runs no scripts.
    private static Dictionary<InstanceChangeKey, SourceOverride> Flatten(InstanceChanges changes)
    {
        var flat = new Dictionary<InstanceChangeKey, SourceOverride>();
        foreach (var (path, node) in changes.Nodes)
        {
            foreach (var entry in node.Components.Values.SelectMany(static overrides => overrides.All))
            {
                flat[new InstanceChangeKey(path, entry.Property, entry.Part?.Id ?? Guid.Empty)] = entry;
            }
        }

        return flat;
    }

    // Each value is encoded once per value type of the schema; materials are the only asset changes.
    private static RuntimeInstanceChange ToRuntimeChange(InstanceChangeKey key, SourceOverride entry)
    {
        RuntimeInstanceValue value = entry.Value switch
        {
            AssetPath material when entry.Property == SceneSchema.Geometry.Surface.Material.Id
                => new RuntimeInstanceValue.Asset(MaterialOverridePathMapper.ToEnginePath(material)!),
            bool flag => new RuntimeInstanceValue.Flag(flag),
            int number => new RuntimeInstanceValue.Integer(number),
            float number => new RuntimeInstanceValue.Scalar(number),
            Vector3 vector => new RuntimeInstanceValue.Vector(vector),
            Enum member => new RuntimeInstanceValue.Integer(Convert.ToInt32(member, CultureInfo.InvariantCulture)),
            LodPolicy policy => new RuntimeInstanceValue.LodPolicyValue(policy),
            _ => throw new InvalidOperationException($"Change '{entry.Property}' holds a {entry.Value.GetType().Name}, which has no engine encoding."),
        };
        return new RuntimeInstanceChange(key.Path, ChangeIdOf(key.Property), key.Part, value);
    }

    private static RuntimeOverrideId ChangeIdOf(PropertyId property)
        => SceneWire.TryGetChangeId(property, out var id)
            ? id
            : throw new InvalidOperationException($"Property '{property}' is not a property a scene can change.");

    private static ImmutableArray<RuntimeInstanceChange> ToRuntimeChanges(InstanceChanges changes)
        => [.. Flatten(changes).Select(static pair => ToRuntimeChange(pair.Key, pair.Value))];

    private static void ApplyInstance(WorldDispatch world, SceneNode node, SceneInstanceComponent instance)
        => world.Execute(new RuntimeInstantiateScene(node.Id, InstanceSourcePath(instance.Source), ToRuntimeChanges(instance.Changes)));

    // Brings the engine's instance of a node in step with the scene: the node's instance is built, changed or detached.
    private async Task ProjectInstanceAsync(DocumentLifetime lifetime, NodeId id, CancellationToken cancellationToken)
    {
        var scene = lifetime.Scene;
        var node = FindNode(scene, id);
        var current = node is null ? null : InstanceOf(node);
        var previous = lifetime.Instances.GetValueOrDefault(id);
        if (node is null || current is null)
        {
            if (previous is not null)
            {
                _ = lifetime.Instances.Remove(id);
                this.Report(await this.DetachInstanceAsync(scene, id, cancellationToken).ConfigureAwait(true));
            }

            return;
        }

        lifetime.Instances[id] = new ProjectedInstance(current.Source, current.Changes);
        if (previous is null || !Equals(previous.Source, current.Source))
        {
            this.Report(await this.InstantiateAsync(scene, node, current, cancellationToken).ConfigureAwait(true));
            if (this.structureStore.Find(this.session.Context, current.Source) is null)
            {
                await this.ReadStructuresAsync([current.Source]).ConfigureAwait(true);
            }

            return;
        }

        var commands = ChangeCommands(node, previous.Changes, current.Changes);
        if (commands.Count != 0)
        {
            this.Report(await this.ExecuteInstanceSyncAsync(scene, node, id, world => commands.ForEach(command => world.Execute(command)), cancellationToken).ConfigureAwait(true));
        }
    }

    private Task<SyncOutcome> InstantiateAsync(Scene scene, SceneNode node, SceneInstanceComponent instance, CancellationToken cancellationToken)
        => this.ExecuteInstanceSyncAsync(scene, node, node.Id, world => ApplyInstance(world, node, instance), cancellationToken);

    private Task<SyncOutcome> DetachInstanceAsync(Scene scene, NodeId nodeId, CancellationToken cancellationToken)
        => this.ExecuteInstanceSyncAsync(scene, node: null, nodeId, world => world.Execute(new RuntimeDetachInstance(nodeId)), cancellationToken);

    private Task<SyncOutcome> ExecuteInstanceSyncAsync(Scene scene, SceneNode? node, NodeId nodeId, Action<WorldDispatch> apply, CancellationToken cancellationToken)
        => this.ExecuteNodeSyncAsync(
            scene,
            node,
            nodeId,
            SceneOperationKinds.EditInstance,
            nameof(SceneInstanceComponent),
            LiveSyncDiagnosticCodes.InstanceRejected,
            LiveSyncDiagnosticCodes.InstanceFailed,
            apply,
            cancellationToken);

    /// <summary>The address of one instance change: the node, the property and the part.</summary>
    private readonly record struct InstanceChangeKey(NodePath Path, PropertyId Property, Guid Part);

    /// <summary>The source and changes of an instance as the projector last sent them.</summary>
    private sealed record ProjectedInstance(AssetPath Source, InstanceChanges Changes);
}
