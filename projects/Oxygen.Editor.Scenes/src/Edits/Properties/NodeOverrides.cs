// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>
/// Builds the edits that change nodes named by reference, nodes of the scene and nodes an instance brings in alike, and
/// reads what such a node overrides of its source.
/// </summary>
/// <remarks>
/// A node of the scene takes a value as its own property and overrides a part of its source in its component's
/// overrides. A node an instance brings in takes both as changes of its instance root (scene instances 8.3), written by
/// <see cref="SetOverrides"/> with the node's path, so its source is never changed.
/// </remarks>
public static class NodeOverrides
{
    /// <summary>Builds the edit that sets property values on nodes.</summary>
    /// <param name="values">The values of each node.</param>
    /// <param name="label">The history label.</param>
    /// <returns>
    /// A <see cref="SetProperties"/> for nodes of the scene, a <see cref="SetOverrides"/> for nodes an instance brings in,
    /// or a <see cref="SceneEditBatch"/> of both.
    /// </returns>
    public static ISceneEdit SetValues(IReadOnlyDictionary<NodeReference, PropertyEdit> values, string label)
    {
        ArgumentNullException.ThrowIfNull(values);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        var owned = values.Where(static pair => pair.Key.IsOwned).ToDictionary(static pair => pair.Key.Node, static pair => pair.Value);
        var changes = ChangeWrites(values.Where(static pair => !pair.Key.IsOwned));
        if (changes.Count == 0)
        {
            return new SetProperties(owned, label);
        }

        var instanced = new SetOverrides(changes, label);
        return owned.Count == 0 ? instanced : new SceneEditBatch(label, [new SetProperties(owned, label), instanced]);
    }

    /// <summary>Builds the edit that sets or resets overrides of nodes.</summary>
    /// <param name="writes">The writes of each node; a write's path is ignored, the node's own path applies.</param>
    /// <param name="label">The history label.</param>
    /// <returns>The edit, with the writes of nodes an instance brings in under their instance roots.</returns>
    public static SetOverrides Write(IReadOnlyDictionary<NodeReference, IReadOnlyList<OverrideWrite>> writes, string label)
    {
        ArgumentNullException.ThrowIfNull(writes);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        return new SetOverrides(
            writes
                .GroupBy(static pair => pair.Key.Node)
                .ToDictionary(
                    static group => group.Key,
                    static group => (IReadOnlyList<OverrideWrite>)[.. group.SelectMany(static pair => pair.Value.Select(write => write with { Path = pair.Key.IsOwned ? null : pair.Key.Path }))]),
            label);
    }

    /// <summary>Gets what a node overrides of its source for one component kind.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <param name="componentKind">The component kind, such as <see cref="SceneSchema.Geometry.Kind"/>.</param>
    /// <returns>
    /// For a node of the scene, its component's overrides; for a node an instance brings in, its instance root's changes
    /// of the node. Empty when there are none, or the node or its instance root is not in the scene.
    /// </returns>
    public static SourceOverrides Of(Scene scene, NodeReference node, string componentKind)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentException.ThrowIfNullOrWhiteSpace(componentKind);
        var found = scene.AllNodes.FirstOrDefault(candidate => candidate.Id == node.Node);
        var overrides = node.IsOwned
            ? found?.Components.OfType<IOverridableComponent>()
                .FirstOrDefault(component => string.Equals(component.Overrides.ComponentKind, componentKind, StringComparison.Ordinal))
                ?.Overrides
            : InstanceChangeWrites.InstanceOf(found)?.Changes.Of(node.Path).Of(componentKind);
        return overrides ?? SourceOverrides.For(componentKind);
    }

    /// <summary>Finds what a node overrides of its source for one property.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <param name="property">The property.</param>
    /// <param name="part">The part, or <see langword="null"/> for a property of the component itself.</param>
    /// <returns>The override or change, as <see cref="Of"/> reads it; <see langword="null"/> when there is none.</returns>
    public static SourceOverride? Find(Scene scene, NodeReference node, PropertyId property, PartId? part)
    {
        ArgumentNullException.ThrowIfNull(property);
        return Of(scene, node, property.ComponentKind).Find(property, part);
    }

    /// <summary>Turns the values of nodes an instance brings in into changes of their instance roots.</summary>
    /// <param name="values">The values of each node an instance brings in.</param>
    /// <returns>The writes, by instance root.</returns>
    internal static Dictionary<NodeId, IReadOnlyList<OverrideWrite>> ChangeWrites(IEnumerable<KeyValuePair<NodeReference, PropertyEdit>> values)
        => values
            .GroupBy(static pair => pair.Key.Node)
            .ToDictionary(
                static group => group.Key,
                static group => (IReadOnlyList<OverrideWrite>)[.. group.SelectMany(static pair => pair.Value.Select(entry => new OverrideWrite(entry.Key, Part: null, LayoutRevision: null, entry.Value) { Path = pair.Key.Path }))]);
}
