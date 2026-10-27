// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Writes the effective values of a node an instance brings in onto a <see cref="SceneNode"/>: the transient node the
/// Properties pane shows for it (scene instances 8.5), and the owned node Unpack and Copy make of it (scene instances
/// 6.8).
/// </summary>
/// <remarks>
/// A value resolves as scene instances 6.3 orders it: this scene's change of the node first, then the value read from
/// the mounted content, which holds the source's value and the changes of deeper nesting levels. A change of a component
/// the node does not have changes nothing. A rotation is written as the change's angles where a change sets it, else as
/// the angles the read decomposed (Y-X-Z); either way the node stores the same quaternion.
/// </remarks>
public static class InstancedNodes
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

    /// <summary>
    /// Builds the owned node a node of a model instance becomes: its values with this scene's changes, and its geometry
    /// with the surface and LOD changes as the geometry component's overrides.
    /// </summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="values">The node's values read from the mounted content.</param>
    /// <param name="changes">This scene's changes of the node.</param>
    /// <returns>The node, with a new ID; it is in no scene's tree.</returns>
    /// <exception cref="InvalidOperationException">A value is of a component the editor does not write through its properties.</exception>
    public static SceneNode Own(Scene scene, InstancedNodeValues values, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(values);
        ArgumentNullException.ThrowIfNull(changes);
        // The changes go over the values read, onto every component the node has, as for an authored scene's node.
        var node = new SceneNode(scene, values.Name);
        WriteValues(scene, node, values, InstancedNodeChanges.None);
        WriteGeometry(node, values.Geometry, []);
        Apply(scene, node, changes);
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

        // The values read of the geometry, each surface value with the layout revision it was resolved against.
        var read = values.Values
            .Where(static value => string.Equals(value.Property.ComponentKind, SceneSchema.Geometry.Kind, StringComparison.Ordinal))
            .Select(static value => new SourceOverride(
                value.Property,
                value.Property.PartKind is { } partKind ? new PartId(partKind, value.Part) : null,
                value.LayoutRevision,
                value.Value));
        WriteGeometry(node, values.Geometry, read.Concat(changes.Of(SceneSchema.Geometry.Kind).All));
        WriteValues(scene, node, values, changes);
    }

    /// <summary>
    /// Writes this scene's changes of a node an instance brings in onto the owned copy of that node, as a node of an
    /// authored scene source becomes the scene's own.
    /// </summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="node">The owned copy, holding the source's stored values.</param>
    /// <param name="changes">This scene's changes of the node.</param>
    /// <exception cref="InvalidOperationException">A change is of a component the editor does not write through its properties.</exception>
    public static void Apply(Scene scene, SceneNode node, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(changes);
        foreach (var (kind, changed) in changes.Components)
        {
            if (string.Equals(kind, SceneSchema.Geometry.Kind, StringComparison.Ordinal))
            {
                if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } geometry)
                {
                    geometry.Overrides = changed.All.Aggregate(geometry.Overrides, static (all, value) => all.With(value));
                }

                continue;
            }

            var rules = SceneProperties.RulesOf(kind)
                ?? throw new InvalidOperationException($"Component kind '{kind}' has no property rules to write its values.");
            if (rules.TargetOf(scene, node) is { } target)
            {
                foreach (var (property, value) in changed.Values)
                {
                    rules.Descriptors[property].WriteBoxed(target, value);
                }
            }
        }

        if (changes.Parameters.IsEmpty || node.Components.OfType<ScriptingComponent>().FirstOrDefault() is not { } scripting)
        {
            return;
        }

        scripting.Slots = [.. scripting.Slots.Select(slot => changes.Parameters.GetValueOrDefault(slot.Id) is { } parameters
            ? parameters.Aggregate(slot, static (current, parameter) => current.WithOverride(parameter.Key, parameter.Value))
            : slot)];
    }

    // The values of every component but the geometry: this scene's change of a value, else the value read.
    private static void WriteValues(Scene scene, SceneNode node, InstancedNodeValues values, InstancedNodeChanges changes)
    {
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

    // The geometry, which no change replaces, and the geometry values the node replaces.
    private static void WriteGeometry(SceneNode node, AssetPath? geometry, IEnumerable<SourceOverride> overrides)
    {
        if (geometry is null)
        {
            return;
        }

        var all = overrides.Aggregate(SourceOverrides.For(SceneSchema.Geometry.Kind), static (all, value) => all.With(value));
        if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } component)
        {
            component.Geometry = geometry;
            component.Overrides = all;
            return;
        }

        _ = node.AddComponent(new GeometryComponent
        {
            Name = SceneComponents.Create(SceneComponents.Geometry).Name,
            Geometry = geometry,
            Overrides = all,
        });
    }

    private static object AddComponent(Scene scene, SceneNode node, IComponentRules rules)
    {
        _ = node.AddComponent(SceneComponents.Create(rules.Kind));
        return rules.TargetOf(scene, node)
            ?? throw new InvalidOperationException($"A component of kind '{rules.Kind}' is not where its rules write.");
    }
}
