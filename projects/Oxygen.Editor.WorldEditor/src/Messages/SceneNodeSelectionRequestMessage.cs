// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
///     Represents a request message for selecting scene nodes within a scene graph.
/// </summary>
internal sealed class SceneNodeSelectionRequestMessage : RequestMessage<IList<SceneNode>>
{
    /// <summary>
    ///     Gets the entities that have been selected in the scene graph as a result of the request.
    /// </summary>
    public IList<SceneNode> SelectedEntities => this.Response;
}
