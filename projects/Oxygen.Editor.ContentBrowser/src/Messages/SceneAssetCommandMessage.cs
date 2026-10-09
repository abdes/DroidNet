// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>What the Content Browser asks the scene workspace to do with a saved scene.</summary>
public enum SceneAssetCommand
{
    /// <summary>Rename the scene and its file.</summary>
    Rename,

    /// <summary>Save a copy of the saved scene under a free name such as <c>Main (2)</c> and a new identity.</summary>
    Duplicate,

    /// <summary>Remove the scene from the project and move its file to the Recycle Bin.</summary>
    Delete,
}

/// <summary>
/// Asks the scene workspace to rename, duplicate or delete an editor scene. Scenes are identified by name in the
/// project, so their files change only through the workspace that owns the scene list and open documents.
/// </summary>
/// <param name="command">The requested change.</param>
/// <param name="sceneName">The scene's current name.</param>
/// <param name="newName">The new name for a rename.</param>
/// <param name="recycle">Moves a deleted scene's file to the Recycle Bin.</param>
public sealed class SceneAssetCommandMessage(SceneAssetCommand command, string sceneName, string? newName = null, Func<IReadOnlyList<string>, Task>? recycle = null)
    : AsyncRequestMessage<string?>
{
    /// <summary>Gets the requested change.</summary>
    public SceneAssetCommand Command { get; } = command;

    /// <summary>Gets the scene's current name.</summary>
    public string SceneName { get; } = sceneName;

    /// <summary>Gets the new name for a rename.</summary>
    public string? NewName { get; } = newName;

    /// <summary>Gets the Recycle Bin action for a delete.</summary>
    public Func<IReadOnlyList<string>, Task>? Recycle { get; } = recycle;
}
