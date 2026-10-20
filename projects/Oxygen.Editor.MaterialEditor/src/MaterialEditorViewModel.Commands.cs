// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.MaterialEditor;

/// <content>Undo and Redo of the material: they end the edit in progress and run the document's history.</content>
public sealed partial class MaterialEditorViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
        => (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) && this.UndoCommand.CanExecute(parameter: null))
            || (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) && this.RedoCommand.CanExecute(parameter: null));

    /// <inheritdoc/>
    public Task ExecuteAsync(string commandId)
        => string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal)
            ? this.UndoCommand.ExecuteAsync(parameter: null)
            : this.RedoCommand.ExecuteAsync(parameter: null);
}
