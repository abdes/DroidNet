// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>Builds the candidate state a property edit would give a component, without touching the scene.</summary>
internal static class ComponentCandidates
{
    /// <summary>Copies each edited node's component of a type and writes its edit to the copy.</summary>
    /// <typeparam name="T">The component type.</typeparam>
    /// <param name="scene">The scene.</param>
    /// <param name="edits">The edits.</param>
    /// <param name="descriptors">The descriptors that write the edits.</param>
    /// <returns>Each edited node's source component and its candidate.</returns>
    public static IEnumerable<(NodeId Node, T Source, T Candidate)> Of<T>(
        Scene scene,
        IReadOnlyList<PropertyTargetEdit> edits,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
        where T : GameComponent
    {
        foreach (var edit in edits)
        {
            if (edit.Node is not { } id || scene.AllNodes.FirstOrDefault(node => node.Id == id)?.Components.OfType<T>().FirstOrDefault() is not { } source)
            {
                continue;
            }

            var candidate = (T)GameComponent.CreateAndHydrate(source.Dehydrate());
            PropertyApply.ApplyToTarget(candidate, edit.Edit, descriptors);
            yield return (id, source, candidate);
        }
    }
}
