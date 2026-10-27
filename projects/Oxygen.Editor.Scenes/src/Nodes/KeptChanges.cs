// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Finds an instance's kept changes, the changes whose node its source no longer holds, and the nodes one can be
/// reassigned to (scene instances 6.4, 8.10).
/// </summary>
public static class KeptChanges
{
    /// <summary>Gets the kept changes of an instance.</summary>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="structure">The source's structure, as last read.</param>
    /// <returns>
    /// The changes whose node path the structure does not hold, in path order; none while the structure is not read or
    /// misses the nodes of a scene it could not read, since a change is kept only against a complete source.
    /// </returns>
    public static ImmutableArray<KeptChange> Of(SceneInstanceComponent instance, SceneStructure? structure)
    {
        ArgumentNullException.ThrowIfNull(instance);
        return IsComplete(structure)
            ? [.. instance.Changes.Ordered.Where(entry => structure!.Find(entry.Key) is null).Select(static entry => new KeptChange(entry.Key, entry.Value))]
            : [];
    }

    /// <summary>Gets whether a structure holds every node of its source, so a path it lacks names no node.</summary>
    /// <param name="structure">The structure.</param>
    /// <returns><see langword="true"/> when the structure is read and complete.</returns>
    public static bool IsComplete(SceneStructure? structure) => structure is { Issues.IsEmpty: true };

    /// <summary>Gets a node's names, as a change records them: <c>Body/Door_FL</c>.</summary>
    /// <param name="structure">The source's structure.</param>
    /// <param name="path">The node's path below the instance root.</param>
    /// <returns>The names, or <see langword="null"/> when the structure does not hold the node.</returns>
    public static string? NamesOf(SceneStructure? structure, NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return structure is not null && InstanceNamePaths.Of(structure, path) is { } steps ? InstanceNamePaths.Format(steps) : null;
    }

    /// <summary>Reads the import record of an instance's model, which tells where its kept changes are repaired.</summary>
    /// <param name="project">The open project.</param>
    /// <param name="source">The instance's source.</param>
    /// <returns>The record, or <see langword="null"/> when the source is not an imported model of the project.</returns>
    public static ModelImportSettings? RecordOf(ProjectContext project, AssetPath source)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        return AssetFileTypes.Is(source.Name, AssetFileCategory.Model) ? ModelRecordLookup.TryFind(project, source)?.Settings : null;
    }

    /// <summary>Gets where a kept change is repaired (SI-16).</summary>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="path">The kept change's node path.</param>
    /// <param name="record">The source's model import record, when the source is an imported model.</param>
    /// <returns>
    /// <see cref="KeptChangeRepair.Model"/> for a change made on the model the instance places, whose node its last
    /// imports report as vanished; <see cref="KeptChangeRepair.Instance"/> for any other: a change a swap carried over,
    /// or a change of a scene instance.
    /// </returns>
    public static KeptChangeRepair RepairOf(SceneInstanceComponent instance, NodePath path, ModelImportSettings? record)
    {
        ArgumentNullException.ThrowIfNull(instance);
        ArgumentNullException.ThrowIfNull(path);
        return AssetFileTypes.Is(instance.Source.Name, AssetFileCategory.Model)
            && path.Count == 1
            && instance.Changes.Of(path).CarriedFrom is null
            && LostAnchorOf(record, path) is not null
                ? KeptChangeRepair.Model
                : KeptChangeRepair.Instance;
    }

    /// <summary>Gets the anchor of the model node a kept change lost, from its model import record.</summary>
    /// <param name="record">The model import record.</param>
    /// <param name="path">The kept change's node path.</param>
    /// <returns>The anchor, or <see langword="null"/> when no import reports the node as vanished.</returns>
    public static string? LostAnchorOf(ModelImportSettings? record, NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return record is null || path.Count != 1
            ? null
            : record.VanishedNodes.FirstOrDefault(node => node.NodeId == path.Nodes[0].Value)?.Anchor;
    }

    /// <summary>Gets the anchor of a node a model's last import published.</summary>
    /// <param name="record">The model import record.</param>
    /// <param name="path">The node's path below the instance root.</param>
    /// <returns>The anchor, or <see langword="null"/> when the last import did not publish the node.</returns>
    public static string? PublishedAnchorOf(ModelImportSettings? record, NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return record is null || path.Count != 1
            ? null
            : record.PublishedNodes.FirstOrDefault(node => node.NodeId == path.Nodes[0].Value)?.Anchor;
    }

    /// <summary>
    /// Gets the nodes a kept change can be reassigned to: the nodes new in the model's last import first, then every
    /// node of the source, in structure order (scene instances 6.4).
    /// </summary>
    /// <param name="scene">The scene, whose other instances of the model a repair in the model also reaches.</param>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="change">The kept change.</param>
    /// <param name="structure">The source's structure.</param>
    /// <param name="record">The source's model import record, when the source is an imported model.</param>
    /// <returns>The candidates.</returns>
    public static ImmutableArray<KeptChangeTarget> TargetsOf(Scene scene, SceneInstanceComponent instance, KeptChange change, SceneStructure structure, ModelImportSettings? record)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(instance);
        ArgumentNullException.ThrowIfNull(change);
        ArgumentNullException.ThrowIfNull(structure);
        var repair = RepairOf(instance, change.Path, record);
        var targets = structure.Nodes.Select(node => repair == KeptChangeRepair.Model
                ? ModelTarget(scene, instance.Source, node, structure, record!)
                : new KeptChangeTarget(node.Path, NamesOf(structure, node.Path) ?? node.Name, IsNew: false, Refusal: null, Kept: 0))
            .ToList();
        return [.. targets.Where(static target => target.IsNew), .. targets.Where(static target => !target.IsNew)];
    }

    /// <summary>
    /// Counts the changes this scene makes of a model node under its own ID, in every instance of the model, which become
    /// kept changes when the node continues another (scene instances 6.4).
    /// </summary>
    /// <param name="scene">The scene.</param>
    /// <param name="model">The model's source.</param>
    /// <param name="path">The node's path below the instance root.</param>
    /// <returns>The number of changed values.</returns>
    public static int CountChanges(Scene scene, AssetPath model, NodePath path)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(path);
        return scene.AllNodes
            .SelectMany(static node => node.Components.OfType<SceneInstanceComponent>())
            .Where(instance => instance.Source == model)
            .Sum(instance => KeptChange.CountValues(instance.Changes.Of(path)));
    }

    private static KeptChangeTarget ModelTarget(Scene scene, AssetPath model, StructureNode node, SceneStructure structure, ModelImportSettings record)
    {
        var names = NamesOf(structure, node.Path) ?? node.Name;
        if (PublishedAnchorOf(record, node.Path) is not { } anchor)
        {
            return new KeptChangeTarget(node.Path, names, IsNew: false, Refusal: $"The last import of {model.Name} did not publish '{names}'. Reimport it first.", Kept: 0);
        }

        var isNew = record.NewNodes.Any(entry => entry.NodeId == node.Path.Nodes[0].Value);
        var continued = record.NodeContinuity.FirstOrDefault(entry => string.Equals(entry.Anchor, anchor, StringComparison.Ordinal));
        return new KeptChangeTarget(
            node.Path,
            names,
            isNew,
            continued is null ? null : string.Format(CultureInfo.InvariantCulture, "'{0}' already continues '{1}'.", anchor, continued.Continues),
            isNew ? 0 : CountChanges(scene, model, node.Path));
    }
}
