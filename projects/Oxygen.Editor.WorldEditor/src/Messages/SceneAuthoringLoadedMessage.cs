// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.Messages;

/// <summary>Publishes loaded authoring data independently of runtime availability.</summary>
/// <param name="scene">The loaded authoring scene.</param>
/// <param name="metadata">The open-document instance that requested the load.</param>
internal sealed class SceneAuthoringLoadedMessage(Scene scene, SceneDocumentMetadata metadata)
{
    /// <summary>Gets the loaded authoring scene.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the originating document instance.</summary>
    public SceneDocumentMetadata Metadata { get; } = metadata;
}
