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

    private void FollowRelocation(AssetRelocationChange change)
    {
        if (this.isDisposed || this.document is not { } current || !this.documentService.FollowRelocation(current.DocumentId, change))
        {
            return;
        }

        this.RefreshDocument(current.DocumentId);
        var followed = this.documentService.GetDocument(current.DocumentId);
        this.metadata.MaterialUri = followed.MaterialUri;
        this.metadata.Title = followed.DisplayName;
        this.MaterialUriText = followed.MaterialUri.ToString();
        this.LogFollowedRelocation(followed.MaterialUri);
    }

    /// <summary>Runs the material's follow-up on its UI thread; the relocation calls participants from a worker.</summary>
    private sealed class RelocationParticipant(MaterialEditorViewModel owner, DispatcherQueue? dispatcher) : IAssetRelocationParticipant
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is reported through the operation result or forwarded to the awaiting caller and must not escape")]
        public Task FollowAsync(AssetRelocationChange change)
        {
            if (dispatcher is null || dispatcher.HasThreadAccess)
            {
                owner.FollowRelocation(change);
                return Task.CompletedTask;
            }

            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!dispatcher.TryEnqueue(() =>
            {
                try
                {
                    owner.FollowRelocation(change);
                    completion.SetResult();
                }
                catch (Exception failure)
                {
                    completion.SetException(failure);
                }
            }))
            {
                completion.SetException(new InvalidOperationException("The material editor's thread is no longer available."));
            }

            return completion.Task;
        }
    }
}
