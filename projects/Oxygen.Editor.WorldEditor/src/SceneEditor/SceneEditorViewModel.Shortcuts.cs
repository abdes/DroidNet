// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Editor-wide scene shortcuts the active scene editor runs: tools, framing in the active viewport
/// and duplication, whichever workspace pane has focus.
/// </summary>
public partial class SceneEditorViewModel
{
    private void RegisterShortcutMessages()
        => this.messenger.Register<SceneShortcutMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && !message.HasReceivedResponse && this.IsActiveDocument() && this.RunShortcut(message.Shortcut))
            {
                message.Reply(response: true);
            }
        });

    private bool RunShortcut(SceneShortcut shortcut)
    {
        switch (shortcut)
        {
            case SceneShortcut.SelectTool:
                this.TransformTools.UseSelectToolCommand.Execute(parameter: null);
                return true;
            case SceneShortcut.MoveTool:
                this.TransformTools.UseMoveToolCommand.Execute(parameter: null);
                return true;
            case SceneShortcut.RotateTool:
                this.TransformTools.UseRotateToolCommand.Execute(parameter: null);
                return true;
            case SceneShortcut.ScaleTool:
                this.TransformTools.UseScaleToolCommand.Execute(parameter: null);
                return true;
            case SceneShortcut.FrameSelection when this.GetActiveViewport() is { } viewport:
                _ = viewport.FrameSelectionAsync();
                return true;
            case SceneShortcut.FrameAll when this.GetActiveViewport() is { } viewport:
                _ = viewport.FrameAllAsync();
                return true;
            case SceneShortcut.Duplicate:
                _ = this.RunViewportEditAsync(ViewportEditCommand.Duplicate);
                return true;
            default:
                return false;
        }
    }
}
