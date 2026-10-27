// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>
/// What a read-only scene still takes (model editor 4): a model's preview refuses every edit of the model and of its
/// structure, and takes the edits of how it is looked at, its environment and the properties of its own lights.
/// </summary>
/// <param name="Reason">Why the scene refuses an edit, which the refusal and the panes say.</param>
public sealed record SceneReadOnly(string Reason)
{
    /// <summary>Gets the nodes whose properties the scene takes edits of, such as a model preview's sun.</summary>
    public ImmutableHashSet<NodeId> EditableNodes { get; init; } = [];

    /// <summary>Gets a value indicating whether the scene takes an edit.</summary>
    /// <param name="edit">The edit.</param>
    /// <returns>
    /// <see langword="true"/> for an edit of the scene's environment, and for an edit or a gesture of the properties of
    /// <see cref="EditableNodes"/> only.
    /// </returns>
    public bool Allows(ISceneEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        return edit switch
        {
            SetSceneEnvironment or SetSceneProperties => true,
            SetProperties or SceneAuthoring.GestureTargets
                => edit.InstancedTargets.Count == 0 && edit.Targets.All(this.EditableNodes.Contains),
            _ => false,
        };
    }

    /// <summary>Gets a value indicating whether the scene takes edits of a node's properties.</summary>
    /// <param name="node">The node.</param>
    /// <returns><see langword="true"/> for one of <see cref="EditableNodes"/>.</returns>
    public bool Allows(NodeId node) => this.EditableNodes.Contains(node);
}
