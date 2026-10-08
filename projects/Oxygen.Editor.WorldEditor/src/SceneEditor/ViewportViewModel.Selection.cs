// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// Selecting in the pane: picking the nodes under a click or marquee, and framing the selection or
/// the whole scene with the pane's editor camera.
/// </summary>
public partial class ViewportViewModel
{
    private static readonly TimeSpan NoticeDuration = TimeSpan.FromSeconds(3);

    private int pickGeneration;
    private CancellationTokenSource? noticeDelay;

    /// <summary>Gets a short message about the last framing request, shown briefly over the pane.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsNoticeVisible))]
    public partial string? Notice { get; private set; }

    /// <summary>Gets a value indicating whether <see cref="Notice"/> shows.</summary>
    public bool IsNoticeVisible => this.Notice is not null;

    /// <summary>Gets or sets the receiver of the pane's picks, which applies them to the shared selection.</summary>
    public Action<ViewportSelection>? SelectionPicked { get; set; }

    /// <summary>Gets or sets the source of the selected nodes that <see cref="FrameSelectionAsync"/> frames.</summary>
    public Func<IReadOnlyList<Guid>>? SelectedNodesProvider { get; set; }

    /// <summary>
    /// Picks the nodes inside a rectangle of the pane and hands them to <see cref="SelectionPicked"/>.
    /// A newer pick, or a new view, supersedes a pick still in flight.
    /// </summary>
    /// <param name="rect">The rectangle, in physical pixels of the pane.</param>
    /// <param name="mode">How the picked nodes combine with the selection.</param>
    /// <param name="isMarquee">Whether every node inside the rectangle is picked, rather than the nearest.</param>
    /// <returns>A task that completes when the pick has been applied or dropped.</returns>
    internal async Task PickAsync(RuntimePickRect rect, ViewportSelectionMode mode, bool isMarquee)
    {
        var viewId = this.AssignedViewId;
        if (!viewId.IsValid || this.SelectionPicked is null)
        {
            return;
        }

        var generation = ++this.pickGeneration;
        RuntimePickResult? result;
        try
        {
            result = await this.EngineService.PickViewAsync(viewId, rect).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewPick,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "PICK_FAILED",
                "Viewport selection failed",
                "The runtime could not pick the scene nodes under the pointer.",
                ex);
            return;
        }

        if (result is null || generation != this.pickGeneration || this.isDisposed || this.AssignedViewId != viewId)
        {
            return;
        }

        // Hits come nearest the centre first; the selection's active node is its last.
        var nodes = result.Hits.Select(hit => hit.NodeId).Distinct().ToList();
        IReadOnlyList<Guid> picked = nodes.Count == 0
            ? []
            : isMarquee ? [.. nodes.Skip(1), nodes[0]] : [nodes[0]];
        this.SelectionPicked?.Invoke(new ViewportSelection(picked, mode));
    }

    /// <summary>Clears the selection (Escape with nothing in progress); a pick still in flight is dropped.</summary>
    internal void ClearSelection()
    {
        ++this.pickGeneration;
        this.SelectionPicked?.Invoke(new ViewportSelection([], ViewportSelectionMode.Replace));
    }

    /// <summary>Frames the selected nodes with the pane's editor camera (F).</summary>
    /// <returns>A task that completes when the request has been submitted.</returns>
    internal Task FrameSelectionAsync()
    {
        var nodes = this.SelectedNodesProvider?.Invoke() ?? [];
        if (nodes.Count == 0)
        {
            this.ShowNotice("Nothing selected to frame");
            return Task.CompletedTask;
        }

        return this.FrameAsync(nodes);
    }

    /// <summary>Frames the whole scene with the pane's editor camera (Shift+F).</summary>
    /// <returns>A task that completes when the request has been submitted.</returns>
    internal Task FrameAllAsync() => this.FrameAsync([]);

    /// <summary>Frames nodes with the pane's editor camera; an empty list frames the whole scene.</summary>
    /// <param name="nodeIds">The nodes to frame, with their descendants.</param>
    /// <returns>A task that completes when the request has been submitted.</returns>
    internal async Task FrameAsync(IReadOnlyList<Guid> nodeIds)
    {
        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var outcome = await this.EngineService.FrameViewAsync(this.AssignedViewId, nodeIds).ConfigureAwait(true);
            switch (outcome)
            {
                case RuntimeFramingOutcome.Framed:
                    this.MarkNavigated();
                    break;
                case RuntimeFramingOutcome.ViewingSceneCamera:
                    this.ShowNotice("Framing moves the editor camera; return to it first");
                    break;
                case RuntimeFramingOutcome.NothingToFrame:
                    this.ShowNotice("The selected nodes no longer exist");
                    break;
                case RuntimeFramingOutcome.InvalidBounds:
                    this.ShowNotice("The selection has no valid bounds to frame");
                    break;
                default:
                    break;
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewFrame,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "FRAME_FAILED",
                "Framing failed",
                "The runtime could not frame the selection in this viewport.",
                ex);
        }
    }

    private void ShowNotice(string notice)
    {
        this.CancelNotice();
        this.Notice = notice;
        var delay = new CancellationTokenSource();
        this.noticeDelay = delay;
        _ = this.ClearNoticeAfterDelayAsync(delay);
    }

    private async Task ClearNoticeAfterDelayAsync(CancellationTokenSource delay)
    {
        try
        {
            await Task.Delay(NoticeDuration, delay.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        if (ReferenceEquals(this.noticeDelay, delay))
        {
            this.noticeDelay = null;
            this.Notice = null;
            delay.Dispose();
        }
    }

    private void CancelNotice()
    {
        if (this.noticeDelay is { } delay)
        {
            this.noticeDelay = null;
            delay.Cancel();
            delay.Dispose();
        }
    }
}
