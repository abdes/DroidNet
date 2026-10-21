// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.SceneEditor;

/// <content>
/// The scene editor's commands: tools, framing in the focused viewport, duplication and play. The command service asks
/// the editor when it is the window's active one or when the focus is inside its view.
/// </content>
public partial class SceneEditorViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId)
    {
        if (this.isDisposed)
        {
            return false;
        }

        return commandId switch
        {
            _ when Is(commandId, SceneEditorCommands.SelectTool)
                || Is(commandId, SceneEditorCommands.MoveTool)
                || Is(commandId, SceneEditorCommands.RotateTool)
                || Is(commandId, SceneEditorCommands.ScaleTool)
                || Is(commandId, SceneEditorCommands.Duplicate) => true,
            _ when Is(commandId, SceneEditorCommands.FrameSelection) || Is(commandId, SceneEditorCommands.FrameAll)
                => this.GetActiveViewport() is not null,
            _ when Is(commandId, SceneEditorCommands.Play) || Is(commandId, SceneEditorCommands.StopPlay)
                => this.Play is not null,
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

        if (Is(commandId, SceneEditorCommands.SelectTool))
        {
            this.TransformTools.UseSelectToolCommand.Execute(parameter: null);
        }
        else if (Is(commandId, SceneEditorCommands.MoveTool))
        {
            this.TransformTools.UseMoveToolCommand.Execute(parameter: null);
        }
        else if (Is(commandId, SceneEditorCommands.RotateTool))
        {
            this.TransformTools.UseRotateToolCommand.Execute(parameter: null);
        }
        else if (Is(commandId, SceneEditorCommands.ScaleTool))
        {
            this.TransformTools.UseScaleToolCommand.Execute(parameter: null);
        }
        else if (Is(commandId, SceneEditorCommands.FrameSelection))
        {
            await this.GetActiveViewport()!.FrameSelectionAsync().ConfigureAwait(true);
        }
        else if (Is(commandId, SceneEditorCommands.FrameAll))
        {
            await this.GetActiveViewport()!.FrameAllAsync().ConfigureAwait(true);
        }
        else if (Is(commandId, SceneEditorCommands.Duplicate))
        {
            await this.RunViewportEditAsync(ViewportEditCommand.Duplicate).ConfigureAwait(true);
        }
        else if (Is(commandId, SceneEditorCommands.Play))
        {
            await this.Play!.PlayAsync().ConfigureAwait(true);
        }
        else if (Is(commandId, SceneEditorCommands.StopPlay))
        {
            await this.Play!.StopAsync().ConfigureAwait(true);
        }
    }

    private static bool Is(string commandId, CommandDescriptor command)
        => string.Equals(commandId, command.Id, StringComparison.Ordinal);

    // The viewport's Ctrl+Z, Ctrl+Y and Delete run the shell commands: the focused part, the active editor or a registered
    // handler (the Scene Explorer) takes them, and the active document's history takes Undo and Redo nobody handles.
    private Task RunShellCommandAsync(CommandDescriptor command)
        => this.window is { } window
            ? window.Commands.ExecuteAsync(command.Id, context: null)
            : Task.CompletedTask;
}
