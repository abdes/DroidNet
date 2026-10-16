// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// What a play session asks of the workspace: saving the open documents and bringing panels forward.
/// </summary>
public interface IPlayWorkspace
{
    /// <summary>Saves every document with unsaved changes, through the ordinary Save and its conflict handling.</summary>
    /// <returns>Whether no open document remains unsaved.</returns>
    public Task<bool> SaveAllAsync();

    /// <summary>Brings a panel forward.</summary>
    /// <param name="panel">The panel; <see cref="PlayPanel.None"/> does nothing.</param>
    public void Reveal(PlayPanel panel);

    /// <summary>Activates a scene document and selects one of its nodes.</summary>
    /// <param name="sceneDocumentId">The scene document.</param>
    /// <param name="node">The node.</param>
    /// <returns>Whether the node was selected.</returns>
    public Task<bool> SelectNodeAsync(DocumentId sceneDocumentId, SceneNode node);
}
