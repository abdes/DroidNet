// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Controls;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>
/// The override bars of a section's fields, from what the edited nodes override of their sources: a node an instance
/// brings in by its instance root's changes (scene instances 8.5), a node of the scene by its component's overrides.
/// </summary>
/// <param name="reset">Resets properties to the source's value on the edited nodes.</param>
internal sealed class InspectorFieldOverrides(Action<IReadOnlyList<PropertyId>> reset)
{
    private readonly Dictionary<string, (PropertyId[] Properties, InspectorFieldOverride Field)> fields = [with(StringComparer.Ordinal)];
    private Scene? scene;
    private IReadOnlyList<NodeReference> nodes = [];

    /// <summary>Gets the override bar of a field.</summary>
    /// <param name="properties">The properties the field shows, such as the three axes of a position.</param>
    /// <returns>The field's bar, the same for the same properties.</returns>
    public InspectorFieldOverride Get(params PropertyId[] properties)
    {
        var key = string.Join('|', properties.Select(static property => property.ToString()));
        if (!this.fields.TryGetValue(key, out var entry))
        {
            entry = (properties, new InspectorFieldOverride(() => reset(properties)));
            entry.Field.State = this.StateOf(properties);
            this.fields[key] = entry;
        }

        return entry.Field;
    }

    /// <summary>Shows what the edited nodes override now.</summary>
    /// <param name="current">The scene of the edited nodes, or <see langword="null"/> when none is edited.</param>
    /// <param name="edited">The edited nodes.</param>
    public void Update(Scene? current, IReadOnlyList<NodeReference> edited)
    {
        this.scene = current;
        this.nodes = edited;
        foreach (var (properties, field) in this.fields.Values)
        {
            field.State = this.StateOf(properties);
        }
    }

    /// <summary>Gets the edited nodes that override any of the properties.</summary>
    /// <param name="properties">The properties.</param>
    /// <returns>Each such node with the properties it overrides.</returns>
    public IReadOnlyDictionary<NodeReference, IReadOnlyList<PropertyId>> Overriding(IReadOnlyList<PropertyId> properties)
    {
        var overriding = new Dictionary<NodeReference, IReadOnlyList<PropertyId>>();
        if (this.scene is not { } current)
        {
            return overriding;
        }

        foreach (var node in this.nodes)
        {
            PropertyId[] overridden = [.. properties.Where(property => NodeOverrides.Find(current, node, property, part: null) is not null)];
            if (overridden.Length != 0)
            {
                overriding[node] = overridden;
            }
        }

        return overriding;
    }

    private OverrideState StateOf(IReadOnlyList<PropertyId> properties)
    {
        var count = this.Overriding(properties).Count;
        return count == 0 ? OverrideState.None : count == this.nodes.Count ? OverrideState.Full : OverrideState.Partial;
    }
}
