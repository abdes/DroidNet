// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.TimeMachine;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Owns script slot edits. Every slot change replaces the node's whole slot list, so one undo restores the previous
/// slots exactly. Scripts never run in editor viewports, so nothing is synchronized to the engine.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    private const string ScriptSlotsInvalidCode = DiagnosticCodes.ScenePrefix + "SCRIPT_SLOTS_INVALID";

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditScriptSlotsAsync(
        SceneDocumentCommandContext context,
        NodeId nodeId,
        ImmutableArray<ScriptSlot> slots)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        if (FindNode(context.Scene, nodeId) is not { } node
            || node.Components.OfType<ScriptingComponent>().FirstOrDefault() is not { } scripting)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditScriptSlots,
                ScriptSlotsInvalidCode,
                "Scripts were not changed",
                "The node no longer has a Scripting component.",
                context);
        }

        if (this.RejectLockedTargets(context, SceneOperationKinds.EditScriptSlots, [node]) is { } lockFailure)
        {
            return lockFailure;
        }

        if (ValidateScriptSlots(slots) is { } error)
        {
            return this.ValidationFailure(SceneOperationKinds.EditScriptSlots, ScriptSlotsInvalidCode, "Scripts were not changed", error, context);
        }

        var before = scripting.Slots;
        if (before.SequenceEqual(slots))
        {
            return SceneCommandResult.Success;
        }

        scripting.Slots = slots;
        context.History.AddChange("Restore Scripts", async () =>
            await this.ApplyScriptSlotsForHistoryAsync(context, scripting, before, slots).ConfigureAwait(true));
        _ = this.AdvanceRevision(context);
        return SceneCommandResult.Success;
    }

    private static string? ValidateScriptSlots(ImmutableArray<ScriptSlot> slots)
    {
        if (slots.IsDefault)
        {
            return "The slot list is missing.";
        }

        if (slots.Any(static slot => slot is null || slot.Id == Guid.Empty))
        {
            return "Every script slot needs an identity.";
        }

        if (slots.Select(static slot => slot.Id).Distinct().Count() != slots.Length)
        {
            return "Two script slots share one identity.";
        }

        foreach (var slot in slots)
        {
            if (slot.Script is { } script && ValidateTypedReferences([script], AssetFileTypes.Script) is { } scriptError)
            {
                return scriptError;
            }

            foreach (var (name, value) in slot.Overrides)
            {
                if (string.IsNullOrWhiteSpace(name))
                {
                    return "A script parameter value has no parameter name.";
                }

                if (value.Kind == ScriptValueKind.Asset && (value.AssetUri is not { IsAbsoluteUri: true } asset
                    || !string.Equals(asset.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)))
                {
                    return $"Parameter '{name}' must reference an asset by its '{AssetUris.Scheme}' URI.";
                }
            }
        }

        return null;
    }

    private async Task ApplyScriptSlotsForHistoryAsync(
        SceneDocumentCommandContext context,
        ScriptingComponent scripting,
        ImmutableArray<ScriptSlot> slots,
        ImmutableArray<ScriptSlot> inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        scripting.Slots = this.RedirectCaptured(slots);
        context.History.AddChange("Reapply Scripts", async () =>
            await this.ApplyScriptSlotsForHistoryAsync(context, scripting, inverse, slots).ConfigureAwait(true));
        _ = this.AdvanceRevision(context);
    }

    private ImmutableArray<ScriptSlot> RedirectCaptured(ImmutableArray<ScriptSlot> slots)
        => SceneReferenceRetargeting.Retarget(slots, this.RedirectOrNull) ?? slots;
}
