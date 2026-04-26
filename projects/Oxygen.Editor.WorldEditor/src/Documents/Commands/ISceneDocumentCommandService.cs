// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Command-shaped entry point for ED-M03 scene document mutations.
/// </summary>
public interface ISceneDocumentCommandService
{
    public Task<SceneCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind);

    public Task<SceneCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind);

    public Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context);

    public Task<SceneCommandResult> RenameItemAsync(
        SceneDocumentCommandContext context,
        ITreeItem item,
        string newName);
}
