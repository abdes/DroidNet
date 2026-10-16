// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Keeps the open scene in step with relocations without reloading it: as a relocation participant its document follows
/// its file and re-points the affected references in memory. The runtime keeps showing the re-pointed nodes as they were
/// until the asset feed reports their new assets mounted, whoever cooks them.
/// </summary>
public partial class SceneEditorViewModel
{
    private readonly RelocatedRuntimeRefresh relocatedRuntime = new();
    private IDisposable? relocationParticipant;
    private IDisposable? relocatedContent;

    private void RegisterRelocationMessages()
    {
        if (this.container.Resolve<IAssetRelocationService>(IfUnresolved.ReturnDefault) is { } relocation)
        {
            this.relocationParticipant = relocation.AddParticipant(new RelocationParticipant(this, DispatcherQueue.GetForCurrentThread()));
        }
        else
        {
            this.LogRelocationUnavailable(this.Document.Title);
        }
    }

    private void DisposeRelocationParticipant()
    {
        this.relocationParticipant?.Dispose();
        this.relocationParticipant = null;
        this.relocatedContent?.Dispose();
        this.relocatedContent = null;
        this.relocatedRuntime.Clear();
    }

    private async Task FollowRelocationAsync(AssetRelocationChange change)
    {
        if (this.isDisposed || this.scene is null
            || this.container.Resolve<IProjectContextService>(IfUnresolved.ReturnDefault)?.ActiveProject is not { } project)
        {
            this.LogRelocationSkipped(this.Document.Title, "the scene is not loaded");
            return;
        }

        if (DocumentRelocations.Map(this.Document, change, project, out var skipped) is not { } relocation)
        {
            this.LogRelocationSkipped(this.Document.Title, skipped!);
            return;
        }

        // The content names what the engine must show again as it applies the relocation.
        SceneRelocatedEventArgs? relocated = null;
        void OnRelocated(object? sender, SceneRelocatedEventArgs args) => relocated = args;
        var content = (SceneContent)this.Document.Content;
        content.Relocated += OnRelocated;
        try
        {
            await this.Document.FollowRelocationAsync(relocation).ConfigureAwait(true);
        }
        finally
        {
            content.Relocated -= OnRelocated;
        }

        this.RefreshPlayScene();
        if (relocated is { } runtime && (runtime.GeometryNodes.Count != 0 || runtime.Environment))
        {
            this.relocatedRuntime.Add(new SceneRelocationResult(Succeeded: true, runtime.GeometryNodes, runtime.Environment));
            this.LogRelocatedContentAwaited(this.Document.Title, runtime.GeometryNodes.Count, runtime.Environment);
            this.ObserveRelocatedContent();
        }
    }

    // The feed publishes its current entries on subscription, so assets that are already mounted show at once.
    private void ObserveRelocatedContent()
    {
        if (this.relocatedContent is not null)
        {
            return;
        }

        if (this.container.Resolve<IAssetFeed>(IfUnresolved.ReturnDefault) is not { } feed)
        {
            this.LogRelocatedContentUnobservable(this.Document.Title);
            this.relocatedRuntime.Clear();
            return;
        }

        var dispatcher = DispatcherQueue.GetForCurrentThread();
        this.relocatedContent = feed.Items.Subscribe(entries =>
        {
            if (dispatcher is null)
            {
                _ = this.ShowMountedRelocationsAsync(entries);
            }
            else
            {
                _ = dispatcher.TryEnqueue(() => _ = this.ShowMountedRelocationsAsync(entries));
            }
        });
    }

    private async Task ShowMountedRelocationsAsync(IReadOnlyList<AssetEntry> entries)
    {
        if (this.isDisposed || this.scene is not { } current || !this.relocatedRuntime.IsPending)
        {
            return;
        }

        var ready = this.relocatedRuntime.TakeReady(current, uri => entries.Find(uri)?.RuntimeAvailability
            is AssetRuntimeAvailability.Mounted or AssetRuntimeAvailability.NotApplicable);
        if (!this.relocatedRuntime.IsPending)
        {
            this.relocatedContent?.Dispose();
            this.relocatedContent = null;
        }

        if (ready.Nodes.Count != 0 || ready.Environment)
        {
            await this.commandService.RefreshRelocatedRuntimeAsync(this.CreateCommandContext(), ready.Nodes, ready.Environment).ConfigureAwait(true);
        }
    }

    /// <summary>Runs the scene's follow-up on its UI thread; the relocation calls participants from a worker.</summary>
    private sealed class RelocationParticipant(SceneEditorViewModel owner, DispatcherQueue? dispatcher) : IAssetRelocationParticipant
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is reported through the operation result or forwarded to the awaiting caller and must not escape")]
        public Task FollowAsync(AssetRelocationChange change)
        {
            if (dispatcher?.HasThreadAccess != false)
            {
                return owner.FollowRelocationAsync(change);
            }

            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!dispatcher.TryEnqueue(() => _ = this.FollowAndCompleteAsync(change, completion)))
            {
                completion.SetException(new InvalidOperationException("The scene editor's thread is no longer available."));
            }

            return completion.Task;
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is forwarded to the awaiting caller and must not escape")]
        private async Task FollowAndCompleteAsync(AssetRelocationChange change, TaskCompletionSource completion)
        {
            try
            {
                await owner.FollowRelocationAsync(change).ConfigureAwait(true);
                completion.SetResult();
            }
            catch (Exception failure)
            {
                completion.SetException(failure);
            }
        }
    }
}
