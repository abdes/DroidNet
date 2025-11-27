// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message indicating that the selection of scene nodes has changed.
/// </summary>
/// <param name="selectedEntities">The list of selected <see cref="SceneNode"/> entities.</param>
internal sealed class SceneNodeSelectionChangedMessage(IList<SceneNode> selectedEntities)
{
    /// <summary>
    /// Gets the list of selected <see cref="SceneNode"/> entities.
    /// </summary>
    public IList<SceneNode> SelectedEntities { get; } = selectedEntities;
}
