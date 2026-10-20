// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.World.SceneExplorer;

/// <content>Undo and Redo while the Scene Explorer has the focus: they run its own commands, which reconcile the tree.</content>
public partial class SceneExplorerViewModel : ICommandHandler
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
