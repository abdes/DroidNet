// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
/// Builds the transient node the Properties pane shows for a node an instance brings in (scene instances 8.5): a node in
/// no scene's tree whose components hold the node's effective values, so each section binds to it as to an owned node.
/// </summary>
/// <remarks>
/// A value resolves as scene instances 6.3 orders it: this scene's change of the node first, then the value read from
/// the mounted content, which holds the source's value and the changes of deeper nesting levels. A change of a component
/// the node does not have changes nothing.
/// </remarks>
internal static class InstancedNodes
{
    /// <summary>Builds the transient node of a node an instance brings in.</summary>
    /// <param name="scene">The scene the instance is in.</param>
    /// <param name="reference">The node: its instance root and its path below it.</param>
    /// <param name="values">The node's values read from the mounted content.</param>
    /// <param name="changes">This scene's changes of the node.</param>
    /// <returns>The node, with the ID the node has in its source; it is in no scene's tree.</returns>
    /// <exception cref="ArgumentException">The reference names a node of the scene, not one an instance brings in.</exception>
    /// <exception cref="InvalidOperationException">A value is of a component the editor does not write through its properties.</exception>
    public static SceneNode Build(Scene scene, NodeReference reference, InstancedNodeValues values, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(reference);
        ArgumentNullException.ThrowIfNull(values);
        ArgumentNullException.ThrowIfNull(changes);
        if (reference.IsOwned)
        {
            throw new ArgumentException("The reference names a node of the scene, not one an instance brings in.", nameof(reference));
        }

        var node = new SceneNode(scene, values.Name) { Id = reference.Path.Nodes[^1] };
        Write(scene, node, values, changes);
        return node;
    }

    /// <summary>Writes a transient node's effective values again, as after the scene changed the node.</summary>
    /// <param name="scene">The scene the instance is in.</param>
    /// <param name="node">The transient node <see cref="Build"/> made.</param>
    /// <param name="values">The node's values read from the mounted content.</param>
    /// <param name="changes">This scene's changes of the node.</param>
    /// <exception cref="InvalidOperationException">A value is of a component the editor does not write through its properties.</exception>
    public static void Write(Scene scene, SceneNode node, InstancedNodeValues values, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(values);
        ArgumentNullException.ThrowIfNull(changes);
        WriteGeometry(node, values, changes.Of(SceneSchema.Geometry.Kind));
        foreach (var component in values.Values
            .Where(static value => !string.Equals(value.Property.ComponentKind, SceneSchema.Geometry.Kind, StringComparison.Ordinal))
            .GroupBy(static value => value.Property.ComponentKind, StringComparer.Ordinal))
        {
            var rules = SceneProperties.RulesOf(component.Key)
                ?? throw new InvalidOperationException($"Component kind '{component.Key}' has no property rules to write its values.");
            var target = rules.TargetOf(scene, node) ?? AddComponent(scene, node, rules);
            var changed = changes.Of(component.Key).Values;
            foreach (var value in component)
            {
                rules.Descriptors[value.Property].WriteBoxed(target, changed.GetValueOrDefault(value.Property, value.Value));
            }
        }
    }

    // The geometry, which no change replaces, and the geometry values the node replaces: the values read, each surface
    // value with the layout revision it was resolved against, then this scene's changes of them.
    private static void WriteGeometry(SceneNode node, InstancedNodeValues values, SourceOverrides changes)
    {
        if (values.Geometry is not { } geometry)
        {
            return;
        }

        var overrides = values.Values
            .Where(static value => string.Equals(value.Property.ComponentKind, SceneSchema.Geometry.Kind, StringComparison.Ordinal))
            .Select(static value => new SourceOverride(
                value.Property,
                value.Property.PartKind is { } partKind ? new PartId(partKind, value.Part) : null,
                value.LayoutRevision,
                value.Value))
            .Concat(changes.All)
            .Aggregate(SourceOverrides.For(SceneSchema.Geometry.Kind), static (all, value) => all.With(value));
        if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } component)
        {
            component.Geometry = geometry;
            component.Overrides = overrides;
            return;
        }

        _ = node.AddComponent(new GeometryComponent
        {
            Name = SceneComponents.Create(SceneComponents.Geometry).Name,
            Geometry = geometry,
            Overrides = overrides,
        });
    }

    private static object AddComponent(Scene scene, SceneNode node, IComponentRules rules)
    {
        _ = node.AddComponent(SceneComponents.Create(rules.Kind));
        return rules.TargetOf(scene, node)
            ?? throw new InvalidOperationException($"A component of kind '{rules.Kind}' is not where its rules write.");
    }
}
