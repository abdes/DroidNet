// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.Messages;

internal class SceneNodeSelectionRequestMessage : RequestMessage<IList<SceneNode>>
{
    public IList<SceneNode> SelectedEntities => this.Response;
}
