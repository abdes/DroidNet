// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Resets one parameter of a script slot to its script's declared default, by removing the stored value.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot.</param>
/// <param name="Parameter">The parameter name.</param>
public sealed record ResetScriptValue(NodeId Node, ScriptSlotId Slot, string Parameter) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Reset Script Value";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (ScriptSlotEdit.Check(scope, this.Node, this.Slot) is { } invalid)
        {
            return invalid;
        }

        return string.IsNullOrWhiteSpace(this.Parameter)
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "A script parameter value has no parameter name.", this.Node))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
        => ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot.WithoutOverrides([this.Parameter]));
}
