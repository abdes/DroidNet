// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Editor-view presentation authoring for <see cref="SceneDocumentCommandService"/>: the "Show in
/// Editor" toggle is recorded as history here, the single mutation and history authority, while the
/// state itself stays workspace-owned and never touches saved scene content.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc />
    public Task<SceneCommandResult> SetEditorHiddenAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        bool hidden)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            var suspendedId = this.PublishSceneFailure(
                SceneOperationKinds.NodeSetEditorHidden,
                DiagnosticCodes.ScenePrefix + "AUTHORING_SUSPENDED",
                "Editor visibility was not changed",
                "The scene is reloading or was replaced, so hide/show did not run.",
                context);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, suspendedId));
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);

        if (this.interaction is not { } service)
        {
            var missingId = this.PublishSceneFailure(
                SceneOperationKinds.NodeSetEditorHidden,
                DiagnosticCodes.SettingsPrefix + "WORKSPACE_STATE_UNAVAILABLE",
                "Editor visibility is unavailable",
                "No workspace interaction service is composed, so the scene explorer cannot hide or show nodes.",
                context,
                domain: FailureDomain.Settings);
            return Task.FromResult(new SceneCommandResult(Succeeded: false, missingId));
        }

        var targets = nodeIds.Where(static id => id != Guid.Empty).Distinct().ToArray();
        if (targets.Length == 0)
        {
            return Task.FromResult(new SceneCommandResult(Succeeded: false));
        }

        // Precompute the batch that actually changes so an idempotent request records no history step.
        var affected = targets.Where(id => service.IsHidden(id) != hidden).ToArray();
        if (affected.Length == 0)
        {
            return Task.FromResult(SceneCommandResult.Success);
        }

        foreach (var nodeId in affected)
        {
            service.SetHidden(nodeId, hidden);
        }

        var label = hidden ? $"Hide {affected.Length} node(s) in editor" : $"Show {affected.Length} node(s) in editor";

        // The recorded step applies the inverse FIRST, matching the layout-history contract: after a
        // forward hide the next undo must SHOW the ids, so toShow carries the batch and toHide is empty
        // (and vice versa after a forward show). Getting this backwards makes undo re-apply the forward
        // state — which is exactly what the failing undo test caught.
        this.RecordEditorHiddenHistory(
            context,
            label,
            toHide: hidden ? [] : affected,
            toShow: hidden ? affected : []);

        return Task.FromResult(SceneCommandResult.Success);
    }

    private void RecordEditorHiddenHistory(
        SceneDocumentCommandContext context,
        string label,
        IReadOnlyList<Guid> toHide,
        IReadOnlyList<Guid> toShow)
        => context.History.AddChange(
            label,
            async () => await this.ApplyEditorHiddenForHistoryAsync(context, label, toHide, toShow).ConfigureAwait(true));

    private async Task ApplyEditorHiddenForHistoryAsync(
        SceneDocumentCommandContext context,
        string label,
        IReadOnlyList<Guid> toHide,
        IReadOnlyList<Guid> toShow)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null || this.interaction is not { } service)
        {
            return;
        }

        foreach (var nodeId in toHide)
        {
            service.SetHidden(nodeId, isHidden: true);
        }

        foreach (var nodeId in toShow)
        {
            service.SetHidden(nodeId, isHidden: false);
        }

        // Register the opposite direction, the same ping-pong the layout history uses, so repeated
        // undo/redo keeps alternating without rebuilding the command.
        this.RecordEditorHiddenHistory(context, label, toShow, toHide);

        // Deliberate: no MarkDirtyAsync and no live-sync request. Editor hide is editing-view
        // presentation state; the ratified contract (D8 R4) makes it undoable while leaving the
        // document clean, the saved source untouched and cooking undemanded.
        await Task.CompletedTask;
    }
}
