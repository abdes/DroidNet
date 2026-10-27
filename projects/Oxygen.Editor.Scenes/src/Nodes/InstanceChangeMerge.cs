// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Merges an outer scene's changes of a node into the changes an inner instance root already makes of it, as Unpack
/// re-roots this scene's changes onto a nested instance and <i>Apply to source</i> moves them into a source's instance.
/// </summary>
internal static class InstanceChangeMerge
{
    /// <summary>Merges changes of one node: the outer scene's values win, the inner names and origin stay.</summary>
    /// <param name="inner">The inner instance root's changes of the node.</param>
    /// <param name="outer">The outer scene's changes of the node.</param>
    /// <param name="levels">How many nesting levels the outer path has above the inner instance root.</param>
    /// <returns>The merged changes.</returns>
    public static InstancedNodeChanges Over(InstancedNodeChanges inner, InstancedNodeChanges outer, int levels)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(outer);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(levels);
        var merged = outer.Components.Values.Aggregate(
            inner,
            static (all, changes) => all.With(changes.All.Aggregate(all.Of(changes.ComponentKind), static (kind, value) => kind.With(value))));
        merged = outer.Parameters
            .SelectMany(static slot => slot.Value.Select(parameter => (Slot: slot.Key, parameter.Key, parameter.Value)))
            .Aggregate(merged, static (all, parameter) => all.WithParameter(parameter.Slot, parameter.Key, parameter.Value));
        return merged.WithNames(inner.Names ?? Below(outer.Names, levels));
    }

    // The names of the levels below the first ones: the outer names start with the levels above the inner root.
    private static string? Below(string? names, int levels)
    {
        var rest = names;
        for (var level = 0; level < levels && rest is not null; level++)
        {
            var separator = rest.IndexOf('/', StringComparison.Ordinal);
            rest = separator < 0 ? null : rest[(separator + 1)..];
        }

        return rest;
    }
}
