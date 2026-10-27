// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.ModelEditor;

/// <content>
/// The Model Editor's commands: framing in its viewport (F, Shift+F). The command service asks the editor when it is the
/// window's active one or when the focus is inside its view.
/// </content>
public sealed partial class ModelEditorViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
        => !this.disposed
            && this.Viewport is not null
            && (Is(commandId, ShellCommands.FrameSelection) || Is(commandId, ShellCommands.FrameAll));

    /// <inheritdoc/>
    public async Task ExecuteAsync(string commandId)
    {
        if (!this.CanExecute(commandId))
        {
            return;
        }

        if (Is(commandId, ShellCommands.FrameSelection))
        {
            await this.Viewport!.FrameSelectionAsync().ConfigureAwait(true);
        }
        else
        {
            await this.Viewport!.FrameAllAsync().ConfigureAwait(true);
        }
    }

    private static bool Is(string commandId, CommandDescriptor command)
        => string.Equals(commandId, command.Id, StringComparison.Ordinal);
}
