// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.Messages;

internal class EntitySeletionRequestMessage : RequestMessage<IList<GameEntity>>
{
    public IList<GameEntity> SelectedEntities => this.Response;
}
