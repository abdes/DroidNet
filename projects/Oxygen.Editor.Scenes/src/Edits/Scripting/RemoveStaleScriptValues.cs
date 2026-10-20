// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Removes the values a slot holds that its script no longer declares, or declares with another type.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
public sealed record RemoveStaleScriptValues : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="RemoveStaleScriptValues"/> class.</summary>
    /// <param name="node">The node whose Scripting component holds the slots.</param>
    /// <param name="slot">The slot.</param>
    /// <param name="declared">The parameters the slot's script declares; the edit keeps its own copy.</param>
    public RemoveStaleScriptValues(NodeId node, ScriptSlotId slot, IReadOnlyList<ScriptParameter> declared)
    {
        ArgumentNullException.ThrowIfNull(declared);
        this.Node = node;
        this.Slot = slot;
        this.Declared = [.. declared];
    }

    /// <summary>Gets the node whose Scripting component holds the slots.</summary>
    public NodeId Node { get; }

    /// <summary>Gets the slot.</summary>
    public ScriptSlotId Slot { get; }

    /// <summary>Gets the parameters the slot's script declares.</summary>
    public IReadOnlyList<ScriptParameter> Declared { get; }

    /// <inheritdoc/>
    public string Label => "Remove Stale Script Values";

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
        => ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot.WithoutOverrides(ScriptSlotRules.FindStale(slot, this.Declared)));
}
