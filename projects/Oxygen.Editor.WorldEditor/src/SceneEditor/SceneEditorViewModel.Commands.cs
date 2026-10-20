// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.SceneEditor;

/// <content>
/// Undo and Redo of the active scene when no focused panel handles them: they run the way the viewport's keys do, through
/// the scene's authoring.
/// </content>
public partial class SceneEditorViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
        => !this.isDisposed
            && ((string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) && this.Document.History.CanUndo)
                || (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) && this.Document.History.CanRedo));

    /// <inheritdoc/>
    public Task ExecuteAsync(string commandId)
    {
        var request = string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) ? SceneEditRequest.Undo : SceneEditRequest.Redo;
        _ = this.messenger.Send(new SceneEditRequestMessage(this.Document.Id, request));
        return Task.CompletedTask;
    }
}
