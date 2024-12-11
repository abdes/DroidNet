// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.Messages;

internal class EntitySelectionChangedMessage(IList<GameEntity> selectedEntities)
{
    public IList<GameEntity> SelectedEntities { get; } = selectedEntities;
}
