// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.SceneExplorer;

/// <content>
/// The commands the explorer runs while it shows the window's active scene document: rename, find, delete, cut, copy
/// and paste, and Undo and Redo, which run its own commands so the tree reconciles. The window's key bindings reach
/// them: the tree has no clipboard shortcuts of its own.
/// </content>
public partial class SceneExplorerViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
    {
        if (this.isDisposed || this.Scene is null || !ReferenceEquals(this.documentArea.ActiveDocument, this.sceneDocument))
        {
            return false;
        }

        return commandId switch
        {
            _ when Is(commandId, ShellCommands.Undo) => this.sceneDocument?.History.CanUndo == true && this.UndoCommand.CanExecute(parameter: null),
            _ when Is(commandId, ShellCommands.Redo) => this.sceneDocument?.History.CanRedo == true && this.RedoCommand.CanExecute(parameter: null),
            _ when Is(commandId, ShellCommands.Delete) => this.DeleteAction.CanExecute(parameter: null),
            _ when Is(commandId, ShellCommands.Cut) => this.CutAction.CanExecute(parameter: null),
            _ when Is(commandId, ShellCommands.Copy) => this.CopyAction.CanExecute(parameter: null),
            _ when Is(commandId, ShellCommands.Paste) => this.PasteAction.CanExecute(parameter: null),
            _ when Is(commandId, SceneExplorerCommands.Rename) => this.RenameAction.CanExecute(parameter: null),
            _ when Is(commandId, SceneExplorerCommands.FindInExplorer) => this.FindRequested is not null,
            _ => false,
        };
    }

    /// <inheritdoc/>
    public async Task ExecuteAsync(string commandId)
    {
        if (!this.CanExecute(commandId))
        {
            return;
        }

        if (Is(commandId, ShellCommands.Undo))
        {
            await this.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
        else if (Is(commandId, ShellCommands.Redo))
        {
            await this.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
        else if (Is(commandId, ShellCommands.Delete))
        {
            await RunAsync(this.DeleteAction).ConfigureAwait(true);
        }
        else if (Is(commandId, ShellCommands.Cut))
        {
            await RunAsync(this.CutAction).ConfigureAwait(true);
        }
        else if (Is(commandId, ShellCommands.Copy))
        {
            await RunAsync(this.CopyAction).ConfigureAwait(true);
        }
        else if (Is(commandId, ShellCommands.Paste))
        {
            await RunAsync(this.PasteAction).ConfigureAwait(true);
        }
        else if (Is(commandId, SceneExplorerCommands.Rename))
        {
            this.RenameAction.Execute(parameter: null);
        }
        else if (Is(commandId, SceneExplorerCommands.FindInExplorer))
        {
            this.FindRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    private static bool Is(string commandId, CommandDescriptor command)
        => string.Equals(commandId, command.Id, StringComparison.Ordinal);

    private static async Task RunAsync(ICommand command)
    {
        if (command is IAsyncRelayCommand asynchronous)
        {
            await asynchronous.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
        else
        {
            command.Execute(parameter: null);
        }
    }
}
