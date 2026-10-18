// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Settings;

/// <summary>Replaces a node's script slots, in order.</summary>
/// <remarks>Scripts never run in editor viewports, so nothing reaches the engine.</remarks>
/// <param name="Node">The node whose Scripting component holds the slots.</param>
/// <param name="Slots">The complete slots, in order.</param>
public sealed record SetScriptSlots(NodeId Node, ImmutableArray<ScriptSlot> Slots) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => "Edit Scripts";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (ScriptingOf(scope) is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node no longer has a Scripting component.", this.Node));
        }

        return Check(this.Slots) is { } error
            ? SceneEditValidation.Invalid(error with { Node = this.Node })
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var scripting = this.ScriptingOf(scope)!;
        var before = scripting.Slots;
        var slots = SceneReferenceRetargeting.Retarget(this.Slots, scope.Moved) ?? this.Slots;
        if (before.SequenceEqual(slots))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        scope.WarnIfDeleted(slots.Select(static slot => slot.Script), this.Node);
        scripting.Slots = slots;
        return new SceneEditApplication.Applied(this with { Slots = before }, [new SceneChange.ScriptSlotsChanged(this.Node)]);
    }

    private static SceneDiagnostic? Check(ImmutableArray<ScriptSlot> slots)
    {
        if (slots.IsDefault)
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "The slot list is missing.");
        }

        if (slots.Any(static slot => slot is null || slot.Id == Guid.Empty))
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "Every script slot needs an identity.");
        }

        if (slots.Select(static slot => slot.Id).Distinct().Count() != slots.Length)
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryDuplicate, "Two script slots share one identity.");
        }

        foreach (var slot in slots)
        {
            if (slot.Script is { } script && AssetReferenceRules.Check([script], AssetFileTypes.Script) is { } scriptError)
            {
                return SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, scriptError);
            }

            foreach (var (name, value) in slot.Overrides)
            {
                if (string.IsNullOrWhiteSpace(name))
                {
                    return SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "A script parameter value has no parameter name.");
                }

                if (value.Kind == ScriptValueKind.AssetValue && value.Asset is null)
                {
                    return SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, $"Parameter '{name}' must reference an asset.");
                }
            }
        }

        return null;
    }

    private ScriptingComponent? ScriptingOf(SceneEditScope scope)
        => scope.FindNode(this.Node)?.Components.OfType<ScriptingComponent>().FirstOrDefault();
}
