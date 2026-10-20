// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Sets the execution order of one script slot.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot.</param>
/// <param name="Order">The execution order; lower runs earlier, across all nodes.</param>
public sealed record SetSlotExecutionOrder(NodeId Node, ScriptSlotId Slot, int Order) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Set Slot Execution Order";

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
        => ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot with { ExecutionOrder = this.Order });
}
