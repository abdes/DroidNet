// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Carries the workspace hidden set toward the editing viewports, and tells the user when the
/// runtime cannot suppress them yet.
/// </summary>
/// <remarks>
/// <para>
/// This is the editor-side half of the ratified Q2 answer: the eye operates immediately on
/// workspace state, and while native viewport suppression is unavailable the user is told so once
/// per scene activation instead of staring at a toggle that appears to do nothing. The native
/// filter and its interop/runtime transport (D8a tasks E1–E3 and I1–I4) are deliberately outside
/// editor scope; when they land, <see cref="SupportsEditingViewMask"/> becomes a real capability
/// probe and the dispatch below gains its implementation without touching this service's callers.
/// </para>
/// <para>
/// One mask per scene activation (ratified Q3): the hidden set is not partitioned per viewport, so
/// the warning is likewise emitted once, not once per pane.
/// </para>
/// </remarks>
/// <param name="interaction">The workspace interaction state owner.</param>
/// <param name="results">The visible diagnostic publisher.</param>
/// <param name="status">The workspace diagnostic status.</param>
public sealed partial class EditingViewMaskService(
    WorkspaceInteractionService interaction,
    IOperationResultPublisher results,
    IStatusReducer status) : IDisposable
{
    private Guid warnedSceneId;
    private bool subscribed;

    /// <summary>
    /// Gets a value indicating whether the installed runtime can drop editor-hidden nodes from
    /// editing viewports.
    /// </summary>
    /// <remarks>
    /// False by construction today: no native per-view hidden filter, interop command or managed
    /// runtime transport exists yet. It is a property rather than an <c>if (false)</c> at the call
    /// site so the native work has one place to turn the feature on.
    /// </remarks>
    public bool SupportsEditingViewMask => false;

    /// <summary>Starts observing hidden-set changes.</summary>
    /// <returns>This service, so the composition root can resolve and activate it in one step.</returns>
    public EditingViewMaskService Start()
    {
        if (!this.subscribed)
        {
            interaction.StateChanged += this.OnStateChanged;
            this.subscribed = true;
        }

        return this;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.subscribed)
        {
            interaction.StateChanged -= this.OnStateChanged;
            this.subscribed = false;
        }
    }

    /// <summary>
    /// Pushes the current hidden set to the editing viewports, or reports why it cannot be pushed.
    /// </summary>
    /// <remarks>
    /// The intended dispatch, once the runtime transport lands, is a revisioned full snapshot of
    /// <see cref="WorkspaceInteractionService.HiddenNodeIds"/> targeted at the active scene
    /// activation, ordered after any preceding world commands so a node created in the same batch
    /// is covered (D8a R7/DD3).
    /// </remarks>
    private void OnStateChanged(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.SupportsEditingViewMask)
        {
            return;
        }

        var sceneId = interaction.ActiveSceneId;
        if (sceneId == Guid.Empty || sceneId == this.warnedSceneId)
        {
            return;
        }

        this.warnedSceneId = sceneId;
        _ = SceneOperationResults.PublishWarning(
            results,
            status,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            DiagnosticCodes.SettingsPrefix + "EDITING_VIEW_MASK_UNAVAILABLE",
            "Editor hide applied in workspace",
            "Viewport suppression is pending a runtime capability, so hidden nodes are still drawn in the editing viewports. In-game rendering is unaffected either way.",
            new AffectedScope { DocumentId = sceneId });
    }
}
