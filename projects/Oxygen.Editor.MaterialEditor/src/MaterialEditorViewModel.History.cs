// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Oxygen.Editor.Materials.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Routes control sessions and history commands to this material document only.</summary>
public sealed partial class MaterialEditorViewModel
{
    private EditGesture? activeGesture;
    private bool acceptsInput = true;

    /// <summary>Begins a numeric or color gesture in this document.</summary>
    /// <param name="field">The edited field label.</param>
    /// <param name="interaction">The input interaction.</param>
    public void BeginEditSession(string field, NumberBoxEditInteractionKind interaction)
    {
        if (this.isDisposed || this.isClosing || !this.acceptsInput)
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
        if (!this.isDisposed)
        {
            this.RefreshFromDocument();
        }
        else
        {
            this.RefreshHistoryCommands();
        }
    }

    private bool CanUndo() => this.acceptsInput && !this.isDisposed && !this.isClosing && MaterialEdits.CanUndo(this.document);

    private bool CanRedo() => this.acceptsInput && !this.isDisposed && !this.isClosing && MaterialEdits.CanRedo(this.document);

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
            if (this.isDisposed || this.isClosing || !this.acceptsInput)
            {
                return;
            }

            var result = cancellingGesture ? new MaterialEditResult(Succeeded: true, OperationId: null)
                : this.ApplyOwnEdit(() => undo ? this.edits.Undo(this.document) : this.edits.Redo(this.document));
            this.RefreshFromDocument();
            this.StatusText = result.Succeeded ? string.Empty : "The history change was rejected.";
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private async Task CompleteGestureAsync(EditGesture gesture, bool commit)
    {
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            if (gesture.Session is { } session)
            {
                _ = this.ApplyOwnEdit(() =>
                {
                    MaterialEdits.CompleteEditSession(this.document, session, commit);
                    return new MaterialEditResult(Succeeded: true, OperationId: null);
                });
                if (!this.isDisposed)
                {
                    this.RefreshFromDocument();
                }
            }
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private async Task CompleteAfterIdleAsync(EditGesture gesture, CancellationTokenSource idle)
    {
        try
        {
            await Task.Delay(WheelIdleDelay, idle.Token).ConfigureAwait(true);
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

    // The editor's own edits change the source too; the values the controls hold already show them.
    private MaterialEditResult ApplyOwnEdit(Func<MaterialEditResult> edit)
    {
        this.isApplyingOwnEdit = true;
        try
        {
            return edit();
        }
        finally
        {
            this.isApplyingOwnEdit = false;
        }
    }

    private void RefreshFromDocument(bool refreshValues = true)
    {
        if (refreshValues)
        {
            this.ReadFromContent();
            return;
        }

        this.IsDirty = this.document.IsDirty;
        this.RefreshHistoryCommands();
    }

    private void RefreshHistoryCommands()
    {
        this.UndoCommand.NotifyCanExecuteChanged();
        this.RedoCommand.NotifyCanExecuteChanged();
    }

    private sealed class EditGesture(string field, NumberBoxEditInteractionKind interaction)
    {
        public string Field { get; } = field;

        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public MaterialEditSession? Session { get; set; }

        public CancellationTokenSource? Idle { get; set; }
    }
}
