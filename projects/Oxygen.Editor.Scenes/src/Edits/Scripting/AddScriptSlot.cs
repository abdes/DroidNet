// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Appends an empty script slot to a node's Scripting component.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The identity of the new slot.</param>
public sealed record AddScriptSlot(NodeId Node, ScriptSlotId Slot) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Add Script Slot";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (ScriptSlotEdit.Check(scope, this.Node, slot: null) is { } invalid)
        {
            return invalid;
        }

        if (this.Slot.IsEmpty)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "A script slot needs an identity.", this.Node));
        }

        return ScriptSlotEdit.IndexOf(ScriptSlotEdit.ScriptingOf(scope, this.Node)!.Slots, this.Slot) >= 0
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.EntryDuplicate, "The node already has a script slot with that identity.", this.Node))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
        => ScriptSlotEdit.Apply(this, scope, this.Node, slots => slots.Add(new ScriptSlot { Id = this.Slot.Value }));
}
