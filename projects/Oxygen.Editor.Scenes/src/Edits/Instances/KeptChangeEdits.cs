// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>What the edits of kept changes share: the instance they edit and what they report (scene instances 8.10).</summary>
internal static class KeptChangeEdits
{
    /// <summary>Gets the instance component of an instance root.</summary>
    /// <param name="scope">The scene.</param>
    /// <param name="node">The instance root.</param>
    /// <returns>The component, or <see langword="null"/> when the node is not an instance root.</returns>
    public static SceneInstanceComponent? InstanceOf(SceneEditScope scope, NodeId node)
        => scope.FindNode(node)?.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    /// <summary>Gets the source structure kept changes are told apart against, or why there is none.</summary>
    /// <param name="scope">The scene and its ports.</param>
    /// <param name="node">The instance root.</param>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="refusal">Why the structure cannot be used, or <see langword="null"/> when it can.</param>
    /// <returns>The complete structure, or <see langword="null"/>.</returns>
    public static SceneStructure? StructureOf(SceneEditScope scope, NodeId node, SceneInstanceComponent instance, out SceneDiagnostic? refusal)
    {
        var structure = scope.StructureOf?.Invoke(instance.Source);
        if (KeptChanges.IsComplete(structure))
        {
            refusal = null;
            return structure;
        }

        refusal = SceneDiagnostic.Error(
            SceneEditCodes.InstanceUnavailable,
            $"The contents of {instance.Source.Name} are not available yet, so its kept changes cannot be told apart. Try again once it is cooked.",
            node);
        return null;
    }

    /// <summary>Gets the refusal of a node path that is not a kept change of the instance now.</summary>
    /// <param name="node">The instance root.</param>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="structure">The source's complete structure.</param>
    /// <param name="path">The node path.</param>
    /// <returns>The refusal, or <see langword="null"/> when the path is a kept change.</returns>
    public static SceneDiagnostic? RefuseNotKept(NodeId node, SceneInstanceComponent instance, SceneStructure structure, NodePath? path)
    {
        if (path?.IsEmpty != false || !instance.Changes.Nodes.ContainsKey(path))
        {
            return SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, "The instance has no such change now. Read its kept changes again.", node);
        }

        return structure.Find(path) is null
            ? null
            : SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, $"{instance.Changes.Of(path).Names ?? path.ToString()} is not a kept change: {instance.Source.Name} holds its node.", node);
    }

    /// <summary>Gets what changed when an instance's changes went from one set to another.</summary>
    /// <param name="node">The instance root.</param>
    /// <param name="before">The changes before.</param>
    /// <param name="after">The changes after.</param>
    /// <returns>The scene change, naming every property either set changes.</returns>
    public static SceneChange Changed(NodeId node, InstanceChanges before, InstanceChanges after)
        => new SceneChange.OverridesChanged(
            node,
            [.. before.Nodes.Values.Concat(after.Nodes.Values)
                .SelectMany(static changes => changes.Components.Values)
                .SelectMany(static component => component.All)
                .Select(static value => value.Property)
                .Distinct()]);
}
