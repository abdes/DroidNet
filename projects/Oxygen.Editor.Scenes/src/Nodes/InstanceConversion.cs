// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Makes owned nodes of the nodes an instance brings in, with their resolved values (scene instances 6.2, 6.8): the
/// contents <see cref="UnpackInstance"/> puts under the instance root, and the nodes Copy puts on the clipboard.
/// </summary>
/// <remarks>
/// <para>
/// An authored scene source gives its stored nodes, read from its file, and this scene's changes are written over them; a
/// node that is itself an instance root stays one, its changes and this scene's changes below it merged, this scene's
/// winning. A model gives the values the mounted content holds, read node by node, with this scene's changes; its
/// geometry keeps referencing the same geometry asset, with the surface and LOD changes as the geometry component's
/// overrides. Every copy gets a new <see cref="NodeId"/>.
/// </para>
/// <para>
/// A source whose nodes cannot be read now, a scene file missing or a model not mounted, refuses the conversion with
/// <see cref="SceneEditCodes.InstanceUnavailable"/>.
/// </para>
/// </remarks>
/// <param name="project">The project whose scene files and mounted content are read.</param>
/// <param name="reader">Reads what a model instance brings in from the mounted content.</param>
public sealed class InstanceConversion(ProjectContext project, IInstancedNodeReader reader)
{
    /// <summary>Builds the edit that unpacks an instance.</summary>
    /// <param name="scene">The scene the instance is in.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="depth">Whether the instances its source holds stay instances or are unpacked too.</param>
    /// <param name="cancellationToken">Cancels the reads.</param>
    /// <returns>The edit, or why the instance cannot be unpacked now.</returns>
    public async Task<InstanceConversionResult<UnpackInstance>> UnpackAsync(Scene scene, NodeId instance, InstanceUnpack depth, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        if (scene.AllNodes.FirstOrDefault(node => node.Id == instance) is not { } root
            || root.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } component)
        {
            return new(Value: null, SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so there is nothing to unpack.", instance));
        }

        var (source, changes) = (component.Source, component.Changes);
        var copies = new List<UnpackedNode>();
        var contents = await this.BuildAsync(scene, source, changes, NodePath.Empty, depth == InstanceUnpack.Complete, only: null, copies, cancellationToken).ConfigureAwait(true);
        return contents is null
            ? new(Value: null, Unavailable(root, source))
            : new(
                new UnpackInstance(instance, depth)
                {
                    Source = source,
                    Changes = changes,
                    Contents = [.. contents.Select(static node => node.Dehydrate())],
                    Copies = copies,
                },
                Refusal: null);
    }

    /// <summary>Unpacks an instance: builds the edit from the instance's source, then runs it.</summary>
    /// <param name="authoring">The authoring of the scene the instance is in.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="depth">Whether the instances its source holds stay instances or are unpacked too.</param>
    /// <param name="cancellationToken">Cancels the reads.</param>
    /// <returns>The edit's result; refused when the instance's contents cannot be read.</returns>
    public async Task<SceneEditResult> ExecuteUnpackAsync(SceneAuthoring authoring, NodeId instance, InstanceUnpack depth, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(authoring);
        var built = await this.UnpackAsync(authoring.Scene, instance, depth, cancellationToken).ConfigureAwait(true);
        return built.Value is { } edit
            ? await authoring.ExecuteAsync(edit).ConfigureAwait(true)
            : SceneEditResult.Of(SceneEditValidation.Refused(built.Refusal!));
    }

    /// <summary>
    /// Copies nodes instances bring in as owned subtrees with their resolved values, linked to nothing: the instances
    /// their subtrees hold are unpacked completely. A node below another copied node goes with it.
    /// </summary>
    /// <param name="scene">The scene the instances are in.</param>
    /// <param name="nodes">The nodes, each by its instance root and path.</param>
    /// <param name="structureOf">What an instance of a source contains, as last read, which tells the nodes' ancestors.</param>
    /// <param name="cancellationToken">Cancels the reads.</param>
    /// <returns>The subtrees, in order, or why the nodes cannot be copied now.</returns>
    public async Task<InstanceConversionResult<IReadOnlyList<SceneNodeData>>> CopyAsync(
        Scene scene,
        IReadOnlyList<NodeReference> nodes,
        Func<AssetPath, SceneStructure?> structureOf,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(structureOf);
        var selected = nodes.ToHashSet();
        var copied = new List<SceneNodeData>();
        foreach (var reference in nodes.Where(node => !node.IsOwned && NodeAncestry.Of(scene, node, structureOf)?.Any(selected.Contains) != true))
        {
            if (scene.AllNodes.FirstOrDefault(node => node.Id == reference.Node) is not { } root
                || root.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } component)
            {
                return new(Value: null, SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The instance the node belongs to no longer exists.", reference.Node));
            }

            var subtree = await this.BuildAsync(scene, component.Source, component.Changes, NodePath.Empty, complete: true, reference.Path, [], cancellationToken).ConfigureAwait(true);
            if (subtree is not [var node])
            {
                return new(Value: null, Unavailable(root, component.Source));
            }

            copied.Add(node.Dehydrate());
        }

        return new(copied, Refusal: null);
    }

    // The diagnostic of a source whose nodes cannot be read now.
    private static SceneDiagnostic Unavailable(SceneNode root, AssetPath source)
        => SceneDiagnostic.Error(
            SceneEditCodes.InstanceUnavailable,
            $"The contents of '{root.Metadata.Name}' are not available yet: {source.Name} could not be read. Try again once it is cooked.",
            root.Id);

    // Moves new children before a node's existing children.
    private static void InsertFirst(SceneNode parent, IReadOnlyList<SceneNode> children)
    {
        var existing = parent.Hierarchy.Children.ToList();
        existing.ForEach(static child => child.Hierarchy.SetParent(newParent: null));
        foreach (var child in children.Concat(existing))
        {
            parent.Hierarchy.AddChild(child);
        }
    }

    // Writes this scene's changes onto the copies of a level's nodes: a change of a node of the level becomes its value,
    // a change deeper down goes to the instance the node of the level holds. A change no copy takes is left to the edit.
    private static void ApplyChanges(Scene scene, InstanceChanges changes, Dictionary<NodeId, SceneNode> copyOf)
    {
        foreach (var (path, nodeChanges) in changes.Nodes)
        {
            if (!copyOf.TryGetValue(path.Nodes[0], out var node))
            {
                continue;
            }

            if (path.Count == 1)
            {
                InstancedNodes.Apply(scene, node, nodeChanges);
            }
            else if (node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } nested)
            {
                var rest = NodePath.Of(path.Nodes.Skip(1));
                nested.Changes = nested.Changes.With(rest, InstanceChangeMerge.Over(nested.Changes.Of(rest), nodeChanges, levels: 1));
            }
        }
    }

    // Builds the owned nodes of one nesting level of an instance: the source's nodes with the changes, recording the copy
    // of each by its path below the unpacked instance root. With a path, only the subtree of that node, however deep.
    // Null when the source cannot be read.
    private async Task<IReadOnlyList<SceneNode>?> BuildAsync(
        Scene scene,
        AssetPath source,
        InstanceChanges changes,
        NodePath prefix,
        bool complete,
        NodePath? only,
        List<UnpackedNode> copies,
        CancellationToken cancellationToken)
        => AssetFileTypes.Is(source.Name, AssetFileCategory.Model)
            ? await this.BuildModelAsync(scene, source, changes, prefix, only, copies, cancellationToken).ConfigureAwait(true)
            : await this.BuildSceneAsync(scene, source, changes, prefix, complete, only, copies, cancellationToken).ConfigureAwait(true);

    // A model's nodes: the values the mounted content holds, with the changes. A model holds no instances.
    private async Task<IReadOnlyList<SceneNode>?> BuildModelAsync(
        Scene scene,
        AssetPath source,
        InstanceChanges changes,
        NodePath prefix,
        NodePath? only,
        List<UnpackedNode> copies,
        CancellationToken cancellationToken)
    {
        if (await reader.ReadStructureAsync(project, source, cancellationToken).ConfigureAwait(true) is not { Issues.IsEmpty: true } structure)
        {
            return null;
        }

        var tops = only is null ? [.. structure.ChildrenOf(parent: null)] : structure.Find(only) is { } node ? [node] : Array.Empty<StructureNode>();
        var roots = new List<SceneNode>();
        foreach (var top in tops)
        {
            if (await OwnAsync(top).ConfigureAwait(true) is not { } root)
            {
                return null;
            }

            roots.Add(root);
        }

        return only is not null && roots.Count == 0 ? null : roots;

        async Task<SceneNode?> OwnAsync(StructureNode node)
        {
            if (await reader.ReadNodeAsync(project, source, node.Path, cancellationToken).ConfigureAwait(true) is not { } values)
            {
                return null;
            }

            var owned = InstancedNodes.Own(scene, values, changes.Of(node.Path));
            copies.Add(new UnpackedNode(NodePath.Of(prefix.Nodes.AddRange(node.Path.Nodes)), owned.Id));
            foreach (var child in structure.ChildrenOf(node.Path))
            {
                if (await OwnAsync(child).ConfigureAwait(true) is not { } copy)
                {
                    return null;
                }

                owned.Hierarchy.AddChild(copy);
            }

            return owned;
        }
    }

    // An authored scene's nodes: its stored values, with the changes; its instances stay instances with the changes
    // below them, or are built in turn when unpacking completely.
    private async Task<IReadOnlyList<SceneNode>?> BuildSceneAsync(
        Scene scene,
        AssetPath source,
        InstanceChanges changes,
        NodePath prefix,
        bool complete,
        NodePath? only,
        List<UnpackedNode> copies,
        CancellationToken cancellationToken)
    {
        if (await SceneFiles.ReadAsync(project, source, cancellationToken).ConfigureAwait(true) is not { } file)
        {
            return null;
        }

        var copied = SceneNodeCopies.Copy(scene, file.RootNodes.Select(static node => node.Dehydrate()));
        List<SceneNode> roots = [.. copied.Roots.Select(data => SceneNode.CreateAndHydrate(scene, data))];
        var byCopy = roots.SelectMany(static root => root.DescendantsAndSelf()).ToDictionary(static node => node.Id);
        var copyOf = copied.Ids.ToDictionary(static entry => entry.Key, entry => byCopy[entry.Value]);
        copies.AddRange(copied.Ids.Select(entry => new UnpackedNode(prefix.Append(entry.Key), entry.Value)));
        ApplyChanges(scene, changes, copyOf);
        var wanted = roots;
        if (only is not null)
        {
            if (!copyOf.TryGetValue(only.Nodes[0], out var target))
            {
                return null;
            }

            // A node deeper down is in the instance the node of this level holds.
            if (only.Count > 1)
            {
                return target.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } holder
                    ? await this.BuildAsync(scene, holder.Source, holder.Changes, prefix.Append(only.Nodes[0]), complete: true, NodePath.Of(only.Nodes.Skip(1)), copies, cancellationToken).ConfigureAwait(true)
                    : null;
            }

            target.Hierarchy.SetParent(newParent: null);
            wanted = [target];
        }

        var sourceIds = copied.Ids.ToDictionary(static entry => entry.Value, static entry => entry.Key);
        return !complete || await this.UnpackNestedAsync(scene, wanted, node => prefix.Append(sourceIds[node.Id]), copies, cancellationToken).ConfigureAwait(true)
            ? wanted
            : null;
    }

    // Replaces each instance in the subtrees with the owned nodes of its source, built in turn; false when a source cannot
    // be read.
    private async Task<bool> UnpackNestedAsync(
        Scene scene,
        IReadOnlyList<SceneNode> roots,
        Func<SceneNode, NodePath> pathOf,
        List<UnpackedNode> copies,
        CancellationToken cancellationToken)
    {
        foreach (var holder in roots.SelectMany(static root => root.DescendantsAndSelf()).Where(static node => node.Components.OfType<SceneInstanceComponent>().Any()).ToList())
        {
            var nested = holder.Components.OfType<SceneInstanceComponent>().First();
            if (await this.BuildAsync(scene, nested.Source, nested.Changes, pathOf(holder), complete: true, only: null, copies, cancellationToken).ConfigureAwait(true) is not { } contents)
            {
                return false;
            }

            _ = holder.RemoveComponent(nested);
            InsertFirst(holder, contents);
        }

        return true;
    }
}
