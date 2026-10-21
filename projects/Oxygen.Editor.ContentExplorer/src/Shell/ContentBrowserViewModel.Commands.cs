// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using DryIoc;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <content>
/// The edit commands wherever the focus is in the Content Browser: Undo and Redo run the global history, which holds the
/// renames, moves and deletes of assets, even while a document is active; Cut and Copy act on the asset list's
/// selection, wherever the user works in the browser, and Paste pastes into the asset list's folder, or into the
/// sources tree's focused folder when the user works there.
/// </content>
public sealed partial class ContentBrowserViewModel : ICommandHandler
{
    /// <inheritdoc/>
    /// <remarks>
    /// The browser keeps Undo and Redo even when there is nothing to replay or a replay still runs, so they never reach a
    /// document's history.
    /// </remarks>
    public bool CanExecute(string commandId)
        => this.CommandOf(commandId) is { } command && (IsReplay(commandId) || command.CanExecute(parameter: null));

    /// <inheritdoc/>
    public Task ExecuteAsync(string commandId)
    {
        var target = this.CommandOf(commandId);
        if (target?.CanExecute(parameter: null) != true)
        {
            return Task.CompletedTask;
        }

        if (target is IAsyncRelayCommand command)
        {
            return command.ExecuteAsync(parameter: null);
        }

        target.Execute(parameter: null);
        return Task.CompletedTask;
    }

    private static bool IsReplay(string commandId)
        => string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal)
            || string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal);

    private ICommand? CommandOf(string commandId)
    {
        if (this.childContainer?.Resolve<AssetsViewModel>(IfUnresolved.ReturnDefault) is not { } assets)
        {
            return null;
        }

        return commandId switch
        {
            _ when string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) => assets.UndoRelocationCommand,
            _ when string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) => assets.RedoRelocationCommand,
            _ when string.Equals(commandId, ShellCommands.Cut.Id, StringComparison.Ordinal) => assets.CutSelectedCommand,
            _ when string.Equals(commandId, ShellCommands.Copy.Id, StringComparison.Ordinal) => assets.CopySelectedCommand,
            _ when string.Equals(commandId, ShellCommands.Paste.Id, StringComparison.Ordinal) => assets.PasteCommand,
            _ => null,
        };
    }
}
