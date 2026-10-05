// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Microsoft.UI;

namespace Oxygen.Editor.World.Documents;

/// <summary>Awaits installation of an approved scene before publishing its accepted workspace identity.</summary>
/// <param name="windowId">The owning workspace window.</param>
/// <param name="scene">The graph whose previous document has already been retired.</param>
/// <param name="metadata">The new document lifetime.</param>
internal sealed class InstallSceneRequestMessage(WindowId windowId, Scene scene, SceneDocumentMetadata metadata)
    : AsyncRequestMessage<bool>
{
    /// <summary>Gets the owning workspace.</summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>Gets the approved replacement graph.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the new document lifetime.</summary>
    public SceneDocumentMetadata Metadata { get; } = metadata;
}
