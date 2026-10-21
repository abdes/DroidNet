// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.SceneInspector;

/// <content>Undo and Redo while the inspector has the focus: they run through the scene's authoring and refresh the fields.</content>
public sealed partial class SceneInspectorViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
        => (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) && this.History is { UndoStack.Count: > 0 })
            || (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) && this.History is { RedoStack.Count: > 0 });

    /// <inheritdoc/>
    public async Task ExecuteAsync(string commandId)
    {
        if (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal))
        {
            await this.UndoAsync().ConfigureAwait(true);
        }
        else if (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal))
        {
            await this.RedoAsync().ConfigureAwait(true);
        }

        this.RefreshPropertyEditorValues();
    }
}
