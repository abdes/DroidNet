// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Coordinates explicit conflict recovery with this document's input and lifetime.</summary>
public sealed partial class MaterialEditorViewModel
{
    private Task pendingConflict = Task.CompletedTask;
    private bool isResolvingConflict;

    /// <inheritdoc/>
    public bool HasSaveConflict { get; private set; }

    /// <inheritdoc/>
    public string SuggestedCopyName => this.DisplayName + " Copy";

    /// <inheritdoc/>
    public Task<DocumentConflictResult> ReloadFromDiskAsync() => this.ResolveConflictAsync(async current =>
    {
        _ = await this.documentService.ReloadAsync(current.DocumentId, CancellationToken.None).ConfigureAwait(true);
        this.HasSaveConflict = false;
        this.assetChanged?.Invoke(this.metadata.MaterialUri);
        return new(Succeeded: true, "Reloaded from disk. Unsaved changes and Undo/Redo history were discarded.");
    });

    /// <inheritdoc/>
    public Task<DocumentConflictResult> SaveCopyAsync(string name)
    {
        var normalized = name.Trim();
        if (string.IsNullOrWhiteSpace(normalized) || normalized.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || normalized.EndsWith('.'))
        {
            return Task.FromResult(new DocumentConflictResult(Succeeded: false, "Enter a valid file name for the new copy."));
        }

        var target = new Uri(this.metadata.MaterialUri, Uri.EscapeDataString(normalized) + ".omat.json");
        return this.ResolveConflictAsync(async current =>
        {
            var saved = await this.documentService.SaveCopyAsync(current.DocumentId, target, CancellationToken.None).ConfigureAwait(true);
            this.assetChanged?.Invoke(saved);
            return new(Succeeded: true, $"Saved “{normalized}” as a separate asset. The original is still unsaved; keep it open or explicitly discard its changes.");
        });
    }

    private Task<DocumentConflictResult> ResolveConflictAsync(Func<MaterialDocument, Task<DocumentConflictResult>> action)
    {
        if (this.isDisposed || this.isResolvingConflict)
        {
            return Task.FromResult(new DocumentConflictResult(Succeeded: false, "The document is unavailable or another recovery action is running."));
        }

        var operation = this.ResolveConflictCoreAsync(action, this.pendingSave);
        this.pendingConflict = operation;
        return operation;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A recovery failure must be returned to the inline prompt without losing document ownership or escaping the UI event.")]
    private async Task<DocumentConflictResult> ResolveConflictCoreAsync(Func<MaterialDocument, Task<DocumentConflictResult>> action, Task<bool>? priorSave)
    {
        this.isResolvingConflict = true;
        this.EndEditSession(NumberBoxEditCompletionKind.Commit);
        this.RefreshHistoryCommands();
        try
        {
            await this.loadTask.ConfigureAwait(true);
            if (priorSave is not null)
            {
                _ = await priorSave.ConfigureAwait(true);
            }

            await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
            try
            {
                if (this.isDisposed || this.document is not { } current)
                {
                    return new(Succeeded: false, "The material document is no longer open.");
                }

                var result = await action(current).ConfigureAwait(true);
                this.StatusText = result.Message;
                return result;
            }
            finally
            {
                _ = this.editGate.Release();
            }
        }
        catch (Exception exception)
        {
            this.StatusText = $"Recovery failed. Your document is still open. {exception.Message}";
            return new(Succeeded: false, this.StatusText);
        }
        finally
        {
            if (!this.isDisposed && this.document is { } current)
            {
                this.RefreshDocument(current.DocumentId);
            }

            this.isResolvingConflict = false;
            this.RefreshHistoryCommands();
        }
    }
}
