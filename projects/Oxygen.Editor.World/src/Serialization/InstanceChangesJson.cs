// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Converts the <see cref="InstanceChanges"/> of an instance root to and from their data, as a scene file and a scene
/// edit hold them.
/// </summary>
public static class InstanceChangesJson
{
    /// <summary>Reads the changes an instance root holds.</summary>
    /// <param name="data">The changes of each node path, or <see langword="null"/> when there are none.</param>
    /// <returns>The changes.</returns>
    /// <exception cref="JsonException">A node path is empty or repeated, or a node's changes change nothing.</exception>
    public static InstanceChanges Read(IList<InstancedNodeChangesData>? data)
    {
        var changes = InstanceChanges.None;
        foreach (var node in data ?? [])
        {
            var path = node.Path.Count == 0
                ? throw new JsonException("An instance change names a node below the instance root: its path cannot be empty.")
                : NodePath.Of(node.Path);
            if (changes.Nodes.ContainsKey(path))
            {
                throw new JsonException($"The instance changes node {path} twice.");
            }

            var entry = InstancedNodeChanges.None.WithNames(node.Names).WithCarriedFrom(node.CarriedFrom);
            foreach (var (kind, overrides) in node.Components)
            {
                var componentChanges = SourceOverridesJson.ReadChanges(kind, overrides);
                entry = componentChanges.IsEmpty
                    ? throw new JsonException($"The changes of the {kind} component of node {path} hold no value.")
                    : entry.With(componentChanges);
            }

            foreach (var (slot, values) in node.Parameters ?? new Dictionary<Guid, IDictionary<string, ScriptValueData>>())
            {
                if (slot == Guid.Empty || values.Count == 0)
                {
                    throw new JsonException($"The parameter changes of node {path} name a script slot and hold a value.");
                }

                foreach (var (name, value) in values)
                {
                    entry = entry.WithParameter(slot, name, ScriptValueJson.Read(name, value));
                }
            }

            changes = entry.IsEmpty
                ? throw new JsonException($"The changes of node {path} change no component and no script parameter.")
                : changes.With(path, entry);
        }

        return changes;
    }

    /// <summary>Writes the changes an instance root holds.</summary>
    /// <param name="changes">The changes.</param>
    /// <returns>The changes of each node path, in path order, or <see langword="null"/> when there are none.</returns>
    public static IList<InstancedNodeChangesData>? Write(InstanceChanges changes)
    {
        ArgumentNullException.ThrowIfNull(changes);
        return changes.IsEmpty ? null : [.. changes.Ordered.Select(static entry => Write(entry.Key, entry.Value))];
    }

    private static InstancedNodeChangesData Write(NodePath path, InstancedNodeChanges changes)
        => new()
        {
            Path = [.. path.Nodes],
            Names = changes.Names,
            CarriedFrom = changes.CarriedFrom,
            Components = changes.Components
                .OrderBy(static entry => entry.Key, StringComparer.Ordinal)
                .ToDictionary(static entry => entry.Key, static entry => SourceOverridesJson.Write(entry.Value)!, StringComparer.Ordinal),
            Parameters = changes.Parameters.IsEmpty
                ? null
                : changes.Parameters.ToDictionary(
                    static slot => slot.Key,
                    static slot => (IDictionary<string, ScriptValueData>)slot.Value.ToDictionary(static value => value.Key, static value => ScriptValueJson.Write(value.Value), StringComparer.Ordinal)),
        };
}
