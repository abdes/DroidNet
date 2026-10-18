// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

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
        var reloaded = await current.ReloadAsync(CancellationToken.None).ConfigureAwait(true);
        if (!reloaded.Succeeded)
        {
            return new(Succeeded: false, $"The file could not be reloaded. {reloaded.Problem}");
        }

        this.HasSaveConflict = false;
        return new(Succeeded: true, "Reloaded from disk. Unsaved changes and Undo/Redo history were discarded.");
    });

    /// <inheritdoc/>
    public Task<DocumentConflictResult> SaveCopyAsync(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var normalized = VirtualPath.NormalizeTypedName(name);
        if (VirtualPath.ValidateSegment(normalized + AssetFileTypes.MaterialDescriptor, VirtualPathRuleSet.Authored) is { } invalid)
        {
            return Task.FromResult(new DocumentConflictResult(Succeeded: false, invalid.ToMessage()));
        }

        var target = this.document.File.Asset.WithName(normalized + AssetFileTypes.MaterialDescriptor);
        if (this.projects.ActiveProject?.Find(target) is not { IsMountRoot: false } location)
        {
            return Task.FromResult(new DocumentConflictResult(Succeeded: false, "The copy would be outside the project's folders."));
        }

        return this.ResolveConflictAsync(async current =>
        {
            var saved = await current.SaveCopyAsync(new DocumentFile(location.File, location.Identity), CancellationToken.None).ConfigureAwait(true);
            if (!saved.Succeeded)
            {
                var reason = saved.Outcome == DocumentSaveOutcome.Conflict
                    ? $"A file named “{normalized}” already exists. Choose another name."
                    : $"The copy was not saved. {saved.Message}";
                return new(Succeeded: false, reason);
            }

            await this.assets.RefreshAsync().ConfigureAwait(true);
            return new(Succeeded: true, $"Saved “{normalized}” as a separate asset. The original is still unsaved; keep it open or explicitly discard its changes.");
        });
    }

    private Task<DocumentConflictResult> ResolveConflictAsync(Func<Document, Task<DocumentConflictResult>> action)
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
    private async Task<DocumentConflictResult> ResolveConflictCoreAsync(Func<Document, Task<DocumentConflictResult>> action, Task<bool>? priorSave)
    {
        this.isResolvingConflict = true;
        this.EndEditSession(NumberBoxEditCompletionKind.Commit);
        this.RefreshHistoryCommands();
        try
        {
            if (priorSave is not null)
            {
                _ = await priorSave.ConfigureAwait(true);
            }

            await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
            try
            {
                if (this.isDisposed)
                {
                    return new(Succeeded: false, "The material document is no longer open.");
                }

                var result = await action(this.document).ConfigureAwait(true);
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
            if (!this.isDisposed)
            {
                this.RefreshFromDocument();
            }

            this.isResolvingConflict = false;
            this.RefreshHistoryCommands();
        }
    }
}
