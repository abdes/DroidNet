// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Builds <see cref="ApplyToSource"/> (scene instances 6.7, 8.3): which authored scenes a node's changes can go to, and
/// what the chosen scene's file becomes with them.
/// </summary>
/// <remarks>
/// <para>
/// The scenes offered are those the node path passes through, outermost first: the instance's source, then, while the
/// path's node at a level is an instance root of an authored scene, the scene it instances. A model is not offered, and
/// holds no instances, so it ends the list.
/// </para>
/// <para>
/// In the chosen scene, a change of a node the scene owns becomes its stored value; a change deeper down becomes a change
/// of the scene's instance root with the rest of the path, over the changes it already makes, the moved values winning.
/// A change whose node the scene does not hold, or whose node lacks the changed component, stays on the instance. A
/// script parameter that names a node of this scene outside the scene's nodes cannot be moved, which refuses the apply.
/// </para>
/// </remarks>
/// <param name="project">The project whose scene files are read.</param>
public sealed class SourceApplication(ProjectContext project)
{
    /// <summary>Gets why changes cannot be applied to a source: a model is not an authored scene.</summary>
    /// <param name="source">The source.</param>
    /// <returns>The reason, or <see langword="null"/> for an authored scene.</returns>
    public static string? RefusalOf(AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return SourceSceneWrite.RefuseModel(source)?.Message;
    }

    /// <summary>Lists the authored scenes a node's changes can be applied to.</summary>
    /// <param name="scene">The scene the instance is in.</param>
    /// <param name="node">The node: an instance root for the whole instance, or a node it brings in.</param>
    /// <param name="cancellationToken">Cancels reading the scene files.</param>
    /// <returns>The scenes, outermost first; empty for a model instance or a node of no instance.</returns>
    public async Task<IReadOnlyList<ApplyTarget>> TargetsAsync(Scene scene, NodeReference node, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        if (InstanceOf(scene, node.Node) is not { } component)
        {
            return [];
        }

        var targets = new List<ApplyTarget>();
        var source = component.Source;
        for (var level = 0; SourceSceneWrite.RefuseModel(source) is null; level++)
        {
            targets.Add(new ApplyTarget(source, level));
            if (level + 1 >= node.Path.Count || await this.NestedSourceAsync(source, node.Path.Nodes[level], cancellationToken).ConfigureAwait(true) is not { } nested)
            {
                break;
            }

            source = nested;
        }

        return targets;
    }

    /// <summary>Builds the edit that applies changes of an instance to an authored scene on their node paths.</summary>
    /// <param name="scene">The scene the instance is in.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="selection">The changes to apply; a scene below the source takes the changes of one node.</param>
    /// <param name="target">The scene, one <see cref="TargetsAsync"/> lists for the selection's node.</param>
    /// <param name="cancellationToken">Cancels reading the scene files.</param>
    /// <returns>The edit, or why the changes cannot be applied now.</returns>
    public async Task<InstanceConversionResult<ApplyToSource>> BuildAsync(Scene scene, NodeId instance, ChangeSelection selection, ApplyTarget target, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(selection);
        ArgumentNullException.ThrowIfNull(target);
        if (scene.AllNodes.FirstOrDefault(node => node.Id == instance) is not { } root
            || root.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } component)
        {
            return Refused(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no changes to apply.", instance));
        }

        if (SourceSceneWrite.RefuseModel(target.Scene, instance) is { } model)
        {
            return Refused(model);
        }

        if (target.Level > 0 && (selection.Node is not { } path || path.Count <= target.Level))
        {
            return Refused(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, $"The changes do not pass through {target.Scene.Name}: apply a node's changes to it.", instance));
        }

        if (await this.ReadTargetAsync(component.Source, selection, target, cancellationToken).ConfigureAwait(true) is not { } file)
        {
            return Refused(SceneDiagnostic.Error(
                SceneEditCodes.InstanceUnavailable,
                $"{target.Scene.Name} could not be read, or '{root.Metadata.Name}' no longer passes through it. Try again.",
                instance));
        }

        var written = Scene.CreateAndHydrate(file.Data);
        var (remaining, kept, refusal) = Move(written, instance, component.Changes, selection, target);
        if (refusal is not null || remaining.Equals(component.Changes))
        {
            return Refused(refusal ?? (kept.Count == 0
                ? SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, $"'{root.Metadata.Name}' has no change to apply to {target.Name}.", instance)
                : SceneDiagnostic.Error(SceneEditCodes.NodePathUnresolved, $"{target.Scene.Name} does not hold the nodes of the changes: {string.Join(", ", kept)}.", instance)));
        }

        return new(
            new ApplyToSource(instance, target.Scene)
            {
                Changes = component.Changes,
                Remaining = remaining,
                Previous = file.Bytes,
                Applied = JsonSerializer.SerializeToUtf8Bytes(new SceneFileData(written.Dehydrate(), file.Data.ExplorerLayout), SceneFileJsonContext.Default.SceneFileData),
                Kept = kept,
            },
            Refusal: null);
    }

    /// <summary>Applies changes of an instance to an authored scene: builds the edit from the scene's file, then runs it.</summary>
    /// <param name="authoring">The authoring of the scene the instance is in.</param>
    /// <param name="instance">The instance root.</param>
    /// <param name="selection">The changes to apply.</param>
    /// <param name="target">The scene.</param>
    /// <param name="cancellationToken">Cancels reading the scene files.</param>
    /// <returns>The edit's result; refused when the changes cannot be applied now.</returns>
    public async Task<SceneEditResult> ExecuteAsync(SceneAuthoring authoring, NodeId instance, ChangeSelection selection, ApplyTarget target, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(authoring);
        var built = await this.BuildAsync(authoring.Scene, instance, selection, target, cancellationToken).ConfigureAwait(true);
        return built.Value is { } edit
            ? await authoring.ExecuteAsync(edit).ConfigureAwait(true)
            : SceneEditResult.Of(SceneEditValidation.Refused(built.Refusal!));
    }

    private static InstanceConversionResult<ApplyToSource> Refused(SceneDiagnostic refusal) => new(Value: null, refusal);

    private static SceneInstanceComponent? InstanceOf(Scene scene, NodeId node)
        => scene.AllNodes.FirstOrDefault(candidate => candidate.Id == node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    // Writes the selected changes into the scene at the target's level: the changes that remain on the instance, the
    // names of those the scene cannot take, or why none can move.
    private static (InstanceChanges Remaining, List<string> Kept, SceneDiagnostic? Refusal) Move(Scene written, NodeId instance, InstanceChanges changes, ChangeSelection selection, ApplyTarget target)
    {
        var remaining = changes;
        var kept = new List<string>();
        foreach (var (changed, node) in changes.Ordered)
        {
            var selected = selection.Select(changed, node);
            if (selected.IsEmpty)
            {
                continue;
            }

            if (MapNodeValues(selected, instance, changed, target.Level) is not { } moved)
            {
                return (changes, kept, SceneDiagnostic.Error(
                    SceneEditCodes.OverrideInvalid,
                    $"A script parameter of {node.Names ?? changed.ToString()} names a node {target.Name} does not hold. Choose a node of {target.Name} first.",
                    instance));
            }

            if (Write(written, NodePath.Of(changed.Nodes.Skip(target.Level)), moved, target.Level))
            {
                remaining = remaining.With(changed, ChangeSelection.Without(node, selected));
            }
            else
            {
                kept.Add(node.Names ?? changed.ToString());
            }
        }

        return (remaining, kept, Refusal: null);
    }

    // Writes one node's changes into the scene: values of a node it owns, or changes of an instance root it holds. False
    // when the scene lacks the node, the instance root or a changed component, which leaves the scene as it was.
    private static bool Write(Scene scene, NodePath path, InstancedNodeChanges changes, int level)
    {
        if (scene.AllNodes.FirstOrDefault(node => node.Id == path.Nodes[0]) is not { } node)
        {
            return false;
        }

        if (path.Count > 1)
        {
            if (node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is not { } nested)
            {
                return false;
            }

            var rest = NodePath.Of(path.Nodes.Skip(1));
            nested.Changes = nested.Changes.With(rest, InstanceChangeMerge.Over(nested.Changes.Of(rest), changes, levels: level + 1));
            return true;
        }

        if (!Holds(scene, node, changes))
        {
            return false;
        }

        InstancedNodes.Apply(scene, node, changes);
        return true;
    }

    // Whether a node has every component and script slot the changes change.
    private static bool Holds(Scene scene, SceneNode node, InstancedNodeChanges changes)
        => changes.Components.Keys.All(kind => string.Equals(kind, SceneSchema.Geometry.Kind, StringComparison.Ordinal)
                ? node.Components.OfType<GeometryComponent>().Any()
                : SceneProperties.RulesOf(kind)?.TargetOf(scene, node) is not null)
            && (changes.Parameters.IsEmpty
                || (node.Components.OfType<ScriptingComponent>().FirstOrDefault() is { } scripting
                    && changes.Parameters.Keys.All(slot => scripting.Slots.Any(candidate => candidate.Id == slot))));

    // Re-points node values at the nodes of the target scene: a node of this instance below the target's level becomes the
    // scene's node, or a node its instance root brings in. Null when a value names a node the target scene does not hold.
    private static InstancedNodeChanges? MapNodeValues(InstancedNodeChanges changes, NodeId instance, NodePath path, int level)
    {
        var above = path.Nodes.Take(level).ToList();
        NodeReference? Map(NodeReference reference)
        {
            if (reference.Node != instance || reference.Path.Count <= level || !reference.Path.Nodes.Take(level).SequenceEqual(above))
            {
                return null;
            }

            var rest = reference.Path.Nodes.Skip(level).ToList();
            return rest.Count == 1 ? NodeReference.To(rest[0]) : new NodeReference(rest[0], NodePath.Of(rest.Skip(1)));
        }

        return changes.Parameters.Values
            .SelectMany(static values => values.Values)
            .Any(value => value.Kind == ScriptValueKind.NodeValue && value.Node is { } reference && Map(reference) is null)
            ? null
            : changes.MapNodes(Map);
    }

    // The source an authored scene's node instances, when the node is an instance root of the scene.
    private async Task<AssetPath?> NestedSourceAsync(AssetPath scene, NodeId node, CancellationToken cancellationToken)
        => SourceSceneWrite.RefuseModel(scene) is null
            && await this.ReadAsync(scene, cancellationToken).ConfigureAwait(true) is { } file
            && Scene.CreateAndHydrate(file.Data).AllNodes.FirstOrDefault(candidate => candidate.Id == node) is { } holder
            && holder.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } nested
                ? nested.Source
                : null;

    // Reads the file of the scene at the target's level, which the path must still pass through.
    private async Task<(byte[] Bytes, SceneFileData Data)?> ReadTargetAsync(AssetPath source, ChangeSelection selection, ApplyTarget target, CancellationToken cancellationToken)
    {
        AssetPath? scene = source;
        for (var level = 0; level < target.Level && scene is not null; level++)
        {
            scene = await this.NestedSourceAsync(scene, selection.Node!.Nodes[level], cancellationToken).ConfigureAwait(true);
        }

        return scene == target.Scene ? await this.ReadAsync(target.Scene, cancellationToken).ConfigureAwait(true) : null;
    }

    // Reads an authored scene's file: its bytes and what they hold. Null when it is not a readable scene of the project.
    private async Task<(byte[] Bytes, SceneFileData Data)?> ReadAsync(AssetPath scene, CancellationToken cancellationToken)
    {
        if (project.FindSource(scene) is not { IsProjectFile: true } location)
        {
            return null;
        }

        try
        {
            var bytes = await project.FileSystem.File.ReadAllBytesAsync(location.File.FullPath, cancellationToken).ConfigureAwait(true);
            return JsonSerializer.Deserialize(bytes, SceneFileJsonContext.Default.SceneFileData) is { } data ? (bytes, data) : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        {
            return null;
        }
    }
}
