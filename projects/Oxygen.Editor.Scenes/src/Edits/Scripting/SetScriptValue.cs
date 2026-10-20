// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Sets one parameter value of a script slot.</summary>
/// <remarks>
/// A slot stores only the values that differ from the script's declared defaults: a value equal to the default, or
/// <see langword="null"/>, removes the override, so later changes to the default apply. Scripts never run in editor
/// viewports, so nothing reaches the engine.
/// </remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slot">The slot.</param>
/// <param name="Parameter">The declared parameter, read from the slot's script.</param>
/// <param name="Value">The value, or <see langword="null"/> to take the default.</param>
public sealed record SetScriptValue(NodeId Node, ScriptSlotId Slot, ScriptParameter Parameter, ScriptValue? Value) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Set Script Value";

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

        // A deserialized edit can still carry null.
        if (this.Parameter is null || string.IsNullOrWhiteSpace(this.Parameter.Name))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "A script parameter value has no parameter name.", this.Node));
        }

        if (ScriptSlotRules.IsDefault(this.Parameter, this.Value) || this.Value is not { } value)
        {
            return SceneEditValidation.Valid;
        }

        if (value.Kind == ScriptValueKind.AssetValue && value.Asset is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, $"Parameter '{this.Parameter.Name}' must reference an asset.", this.Node));
        }

        return ScriptSlotRules.CheckValue(this.Slot.Value, this.Parameter, value) is { } problem
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(problem.Code, problem.Message, this.Node))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (ScriptSlotRules.IsDefault(this.Parameter, this.Value) || this.Value is not { } value)
        {
            return ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot.WithoutOverrides([this.Parameter.Name]));
        }

        var asset = scope.Resolve(value.Asset);
        var resolved = asset is null ? value : ScriptValue.FromAsset(asset);
        return ScriptSlotEdit.Apply(this, scope, this.Node, this.Slot, slot => slot.WithOverride(this.Parameter.Name, resolved), [asset]);
    }
}
