// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Removes one script slot from a node's Scripting component.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot to remove.</param>
public sealed record RemoveScriptSlot(NodeId Node, ScriptSlotId Slot) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Remove Script Slot";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return ScriptSlotEdit.Check(scope, this.Node, this.Slot) ?? SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
        => ScriptSlotEdit.Apply(this, scope, this.Node, slots => slots.RemoveAt(ScriptSlotEdit.IndexOf(slots, this.Slot)));
}
