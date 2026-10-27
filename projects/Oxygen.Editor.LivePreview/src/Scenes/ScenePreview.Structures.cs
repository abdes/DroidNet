// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>
/// What an instance of a source contains (scene instances 8.2) and the effective values of each of its nodes (8.5),
/// read by the engine that shows the active scene from the cooked scene descriptors of the content it mounts: the
/// source's nodes and those of the scenes it instances, without loading geometry or anything else they reference.
/// </summary>
/// <remarks>
/// The structure of every source the shown scene instances is read when the scene becomes ready, when an instance of
/// a source not read yet is placed, and whenever content is mounted again; each read is kept in the session's
/// <see cref="SceneStructureStore"/>, which everything else looks structures up in.
/// </remarks>
internal sealed partial class ScenePreview : IInstancedNodeReader
{
    /// <inheritdoc/>
    async Task<InstancedNodeValues?> IInstancedNodeReader.ReadNodeAsync(ProjectContext project, AssetPath source, NodePath path, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(path);
        if (!ReferenceEquals(this.session.Context, project)
            || AssetFileTypes.SceneProductOf(source) is not { } scene
            || this.activeWorld is not { } world)
        {
            return null;
        }

        var observation = await world.Commands.ReadSceneNodeAsync(Guid.NewGuid(), world.Target, ContentPipelinePaths.ToCookedScenePath(scene), path, cancellationToken).ConfigureAwait(false);
        return observation.Outcome.Succeeded && observation.Values is { Exists: true } values ? ToNodeValues(values) : null;
    }

    /// <summary>Converts the engine's hierarchy of a source's scene into the source's structure.</summary>
    /// <param name="source">The source.</param>
    /// <param name="virtualPath">The virtual path of the source's cooked scene.</param>
    /// <param name="hierarchy">The hierarchy the engine read.</param>
    /// <returns>The structure: each node by its path, its parent's path, the scene defining it and the one it instances.</returns>
    internal static SceneStructure ToStructure(AssetPath source, string virtualPath, RuntimeSceneHierarchy hierarchy)
    {
        var read = new StructureScene(hierarchy.SceneKey, virtualPath);
        var scenes = new Dictionary<Guid, StructureScene> { [read.Key] = read };
        StructureScene SceneOf(Guid key)
        {
            if (!scenes.TryGetValue(key, out var known))
            {
                known = new StructureScene(key, VirtualPath: null);
                scenes[key] = known;
            }

            return known;
        }

        var nodes = hierarchy.Nodes;
        return new SceneStructure(
            source,
            read,
            [
                .. nodes.Select(node => new StructureNode(
                    node.Path,
                    node.Name,
                    node.Parent is { } parent ? nodes[parent].Path : null,
                    SceneOf(node.SceneKey),
                    node.InstanceKey is { } instance ? SceneOf(instance) : null,
                    string.IsNullOrEmpty(node.GeometryPath) ? null : AssetPath.Parse(node.GeometryPath))),
            ],
            [
                .. hierarchy.Issues.Select(issue => new StructureIssue(
                    IssueCodeOf(issue.Code),
                    SceneOf(issue.SceneKey),
                    issue.Node is { } node ? nodes[node].Path : null,
                    issue.SceneKey)),
            ]);
    }

    /// <summary>Converts the engine's values of a node into the values of a node an instance brings in.</summary>
    /// <param name="values">The values the engine read.</param>
    /// <returns>The values, each property's value of the type <see cref="SceneSchema.Changeable"/> gives it.</returns>
    /// <exception cref="InvalidOperationException">The engine read a property the editor's schema does not hold as it does.</exception>
    internal static InstancedNodeValues ToNodeValues(RuntimeSceneNodeValues values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return new InstancedNodeValues(
            values.Name,
            string.IsNullOrEmpty(values.GeometryPath) ? null : AssetPath.Parse(values.GeometryPath),
            [.. values.Properties.Select(ToInstancedValue)]);
    }

    private static InstancedValue ToInstancedValue(RuntimeSceneNodeProperty property)
    {
        if (!SceneWire.TryGetChangeProperty(property.Id, out var id) || !SceneSchema.Changeable.TryGetValue(id, out var type))
        {
            throw new InvalidOperationException($"The engine read property {property.Id}, which the editor's scene schema cannot change.");
        }

        return new InstancedValue(id, property.PartId, property.LayoutRevision, ValueOf(id, type, property.Value));
    }

    // The engine's value in the schema's units: an enum is its member's wire value, an asset its virtual path.
    private static object ValueOf(PropertyId property, Type type, RuntimeInstanceValue value)
        => value switch
        {
            RuntimeInstanceValue.Flag flag when type == typeof(bool) => flag.Value,
            RuntimeInstanceValue.Integer number when type == typeof(int) => number.Value,
            RuntimeInstanceValue.Integer number when type.IsEnum => Enum.ToObject(type, number.Value),
            RuntimeInstanceValue.Scalar number when type == typeof(float) => number.Value,
            RuntimeInstanceValue.Vector vector when type == typeof(Vector3) => vector.Value,
            RuntimeInstanceValue.Asset asset when type == typeof(AssetPath) => AssetPath.Parse(asset.Path),
            RuntimeInstanceValue.LodPolicyValue policy when type == typeof(LodPolicy) => policy.Policy,
            _ => throw new InvalidOperationException($"The engine read a {value.GetType().Name} for '{property}', whose values are {type.Name}."),
        };

    private static string IssueCodeOf(RuntimeSceneHierarchyIssueCode code) => code switch
    {
        RuntimeSceneHierarchyIssueCode.SceneUnavailable => "scene.instance.unavailable",
        RuntimeSceneHierarchyIssueCode.Cycle => "scene.instance.cycle",
        _ => throw new ArgumentOutOfRangeException(nameof(code), code, "The engine reported an unknown hierarchy issue."),
    };

    // The sources a scene instances, each once.
    private static IReadOnlyList<AssetPath> InstancedSources(Scene? scene)
        => scene is null ? [] : [.. scene.AllNodes.Select(InstanceOf).OfType<SceneInstanceComponent>().Select(static instance => instance.Source).Distinct()];

    // Reads the structure of each source through the engine that shows the scene, keeps each one read, and announces
    // them; a source that is not instanceable or not mounted yet keeps its last structure, if any.
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed read keeps the last structure and is logged; the next mount reads again.")]
    private async Task ReadStructuresAsync(IReadOnlyList<AssetPath> sources)
    {
        if (sources.Count == 0 || this.activeWorld is not { } world)
        {
            return;
        }

        var project = this.session.Context;
        var read = new Dictionary<AssetPath, SceneStructure>();
        foreach (var source in sources)
        {
            if (AssetFileTypes.SceneProductOf(source) is not { } scene)
            {
                continue;
            }

            var path = ContentPipelinePaths.ToCookedScenePath(scene);
            try
            {
                var observation = await world.Commands.ReadSceneHierarchyAsync(Guid.NewGuid(), world.Target, path, CancellationToken.None).ConfigureAwait(true);
                if (observation.Outcome.Succeeded && observation.Hierarchy is { Exists: true } hierarchy)
                {
                    read[source] = ToStructure(source, path, hierarchy);
                }
            }
            catch (Exception exception)
            {
                this.LogStructureReadFailed(exception, source);
            }
        }

        this.structureStore.Keep(project, read.Values);
    }

    // A scene shown before its content was mounted stays ready while the content arrives, so the mount itself reads
    // the structures again.
    private void OnContentStatusChanged(object? sender, RuntimeContentChangedEventArgs args)
    {
        _ = sender;
        if (args.Snapshot.State == RuntimeContentState.Mounted)
        {
            this.owner.Queue(() => _ = this.ReadStructuresAsync(InstancedSources(this.activeScene)));
        }
    }
}
