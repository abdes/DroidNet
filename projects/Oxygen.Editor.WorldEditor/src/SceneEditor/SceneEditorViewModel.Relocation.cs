// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Keeps the open scene in step with relocations without reloading it: as a relocation participant its document follows
/// its file and re-points the affected references in memory. The engine projector shows the re-pointed nodes again once
/// their new assets are mounted, whoever cooks them.
/// </summary>
public partial class SceneEditorViewModel
{
    private IDisposable? relocationParticipant;

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

        await this.Document.FollowRelocationAsync(relocation).ConfigureAwait(true);
        this.RefreshPlayScene();
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
