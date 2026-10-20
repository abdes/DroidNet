// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Picks the script of one slot, or clears it.</summary>
/// <remarks>
/// The slot keeps its parameter values; values the new script does not declare become stale, which the inspector offers
/// to remove. Scripts never run in editor viewports, so nothing reaches the engine.
/// </remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot.</param>
/// <param name="Script">The script asset identity, or <see langword="null"/> to clear it.</param>
public sealed record SetSlotScript(NodeId Node, ScriptSlotId Slot, AssetPath? Script) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Set Slot Script";

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

        return this.Script is { } script && AssetReferenceRules.Check([scope.Resolve(script)], AssetFileTypes.Script) is { } error
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, error, this.Node))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var script = scope.Resolve(this.Script);
        return ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot with { Script = script }, [script]);
    }
}
