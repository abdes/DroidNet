// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Keeps the open material in step with asset relocations without closing it: it re-points its texture paths, and its
/// own URI and title when it moved, keeping its undo history and edit state.
/// </summary>
public sealed partial class MaterialEditorViewModel
{
    private readonly IDisposable? relocationParticipant;

    private async Task FollowRelocationAsync(AssetRelocationChange change)
    {
        if (this.isDisposed || this.projects.ActiveProject is not { } project
            || DocumentRelocations.Map(this.document, change, project, out _) is not { } relocation)
        {
            return;
        }

        await this.document.FollowRelocationAsync(relocation).ConfigureAwait(true);
        this.LogFollowedRelocation(this.document.File.AssetUri);
    }

    private sealed class RelocationParticipant(MaterialEditorViewModel owner, DispatcherQueue? dispatcher) : IAssetRelocationParticipant
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is reported through the operation result or forwarded to the awaiting caller and must not escape")]
        public Task FollowAsync(AssetRelocationChange change)
        {
            if (dispatcher is null || dispatcher.HasThreadAccess)
            {
                return owner.FollowRelocationAsync(change);
            }

            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!dispatcher.TryEnqueue(() => _ = FollowOnThreadAsync()))
            {
                completion.SetException(new InvalidOperationException("The material editor's thread is no longer available."));
            }

            return completion.Task;

            async Task FollowOnThreadAsync()
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
}
