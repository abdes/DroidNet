// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Routes control sessions and history commands to this material document only.</summary>
public sealed partial class MaterialEditorViewModel
{
    private MaterialGesture? activeGesture;
    private bool acceptsInput = true;

    /// <summary>Begins a numeric or color gesture in this document.</summary>
    /// <param name="field">The edited field label.</param>
    /// <param name="interaction">The input interaction.</param>
    public void BeginEditSession(string field, NumberBoxEditInteractionKind interaction)
    {
        if (this.isDisposed || this.isClosing || this.isResolvingConflict || !this.acceptsInput || this.document is null)
        {
            return;
        }

        if (this.activeGesture is { } current && string.Equals(current.Field, field, StringComparison.Ordinal) && current.Interaction == interaction)
        {
            current.Idle?.Cancel();
            return;
        }

        this.EndEditSession(NumberBoxEditCompletionKind.Commit);
        this.activeGesture = new(field, interaction);
    }

    /// <summary>Completes input, grouping successive wheel samples until 250 ms idle.</summary>
    /// <param name="args">The control completion.</param>
    public void CompleteEditSession(NumberBoxEditSessionEventArgs args)
    {
        ArgumentNullException.ThrowIfNull(args);
        if (args.InteractionKind == NumberBoxEditInteractionKind.MouseWheel
            && args.CompletionKind == NumberBoxEditCompletionKind.Commit && this.activeGesture is { } gesture)
        {
            gesture.Idle?.Cancel();
            gesture.Idle = new();
            _ = this.CompleteAfterIdleAsync(gesture, gesture.Idle);
            return;
        }

        this.EndEditSession(args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
    }

    /// <summary>Finishes the current gesture exactly once.</summary>
    /// <param name="completion">Whether to keep or restore the preview.</param>
    public void EndEditSession(NumberBoxEditCompletionKind completion)
    {
        if (this.activeGesture is not { } gesture)
        {
            return;
        }

        this.activeGesture = null;
        gesture.Idle?.Cancel();
        _ = this.CompleteGestureAsync(gesture, completion == NumberBoxEditCompletionKind.Commit);
    }

    /// <summary>Ends input before this document's controls are hidden or replaced.</summary>
    public void Deactivate()
    {
        this.acceptsInput = false;
        this.EndEditSession(this.activeGesture?.Interaction == NumberBoxEditInteractionKind.Text
            ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel);
        this.RefreshHistoryCommands();
    }

    /// <summary>Enables input when this document becomes visible again.</summary>
    public void Activate()
    {
        this.acceptsInput = true;
        if (!this.isDisposed && this.document is { } current)
        {
            this.RefreshDocument(current.DocumentId);
        }
        else
        {
            this.RefreshHistoryCommands();
        }
    }

    private bool CanUndo() => this.acceptsInput && !this.isDisposed && !this.isClosing && !this.isResolvingConflict && this.document is { } current && this.documentService.CanUndo(current.DocumentId);

    private bool CanRedo() => this.acceptsInput && !this.isDisposed && !this.isClosing && !this.isResolvingConflict && this.document is { } current && this.documentService.CanRedo(current.DocumentId);

    [RelayCommand(CanExecute = nameof(CanUndo))]
    private Task UndoAsync() => this.ApplyHistoryAsync(undo: true);

    [RelayCommand(CanExecute = nameof(CanRedo))]
    private Task RedoAsync() => this.ApplyHistoryAsync(undo: false);

    private async Task ApplyHistoryAsync(bool undo)
    {
        // An in-progress gesture is itself the user's current undo target.
        var cancellingGesture = undo && this.activeGesture is not null;
        this.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            if (this.isDisposed || this.isClosing || this.isResolvingConflict || !this.acceptsInput || this.document is not { } current)
            {
                return;
            }

            var result = cancellingGesture ? new MaterialEditResult(Succeeded: true, OperationId: null)
                : undo ? this.documentService.Undo(current.DocumentId) : this.documentService.Redo(current.DocumentId);
            this.RefreshDocument(current.DocumentId);
            this.StatusText = result.Succeeded ? string.Empty : "The history change was rejected.";
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private async Task CompleteGestureAsync(MaterialGesture gesture, bool commit)
    {
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            if (gesture.Session is { } session)
            {
                _ = this.documentService.CompleteEditSession(session, commit);
                if (!this.isDisposed && this.document?.DocumentId == session.DocumentId)
                {
                    this.RefreshDocument(session.DocumentId);
                }
            }
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private async Task CompleteAfterIdleAsync(MaterialGesture gesture, CancellationTokenSource idle)
    {
        try
        {
            await Task.Delay(CommitGroupController.DefaultWheelIdleDelay, idle.Token).ConfigureAwait(true);
            if (!idle.IsCancellationRequested && ReferenceEquals(this.activeGesture, gesture) && ReferenceEquals(gesture.Idle, idle))
            {
                this.EndEditSession(NumberBoxEditCompletionKind.Commit);
            }
        }
        catch (OperationCanceledException)
        {
            // A new sample or document transition superseded this idle completion.
        }
        finally
        {
            if (ReferenceEquals(gesture.Idle, idle))
            {
                gesture.Idle = null;
            }

            idle.Dispose();
        }
    }

    private void RefreshDocument(Guid documentId, bool refreshValues = true)
    {
        this.document = this.documentService.GetDocument(documentId);
        this.metadata.IsDirty = this.document.IsDirty;
        this.IsDirty = this.document.IsDirty;
        if (refreshValues)
        {
            this.isLoading = true;
            try
            {
                this.ReadFromDocument(this.document);
            }
            finally
            {
                this.isLoading = false;
            }
        }

        this.RefreshHistoryCommands();
    }

    private void RefreshHistoryCommands()
    {
        this.UndoCommand.NotifyCanExecuteChanged();
        this.RedoCommand.NotifyCanExecuteChanged();
    }

    private sealed class MaterialGesture(string field, NumberBoxEditInteractionKind interaction)
    {
        public string Field { get; } = field;

        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public MaterialEditSession? Session { get; set; }

        public CancellationTokenSource? Idle { get; set; }
    }
}
