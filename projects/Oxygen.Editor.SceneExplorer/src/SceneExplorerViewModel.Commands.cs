// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.World.SceneExplorer;

/// <content>
/// The commands the explorer runs while it shows the window's active scene document: rename, find, delete, and Undo and
/// Redo, which run its own commands so the tree reconciles.
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
            if (this.DeleteAction is IAsyncRelayCommand delete)
            {
                await delete.ExecuteAsync(parameter: null).ConfigureAwait(true);
            }
            else
            {
                this.DeleteAction.Execute(parameter: null);
            }
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
}
