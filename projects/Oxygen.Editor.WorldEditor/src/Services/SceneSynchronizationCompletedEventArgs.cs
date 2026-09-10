// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.Services;

/// <summary>Identifies an accepted scene projection and its open-document owner.</summary>
/// <param name="scene">The authoring scene that was projected.</param>
/// <param name="metadata">The owning document instance.</param>
/// <param name="target">The runtime activation that accepted the projection.</param>
public sealed class SceneSynchronizationCompletedEventArgs(Scene scene, SceneDocumentMetadata metadata, RuntimeSceneTarget target) : EventArgs
{
    /// <summary>Gets the authoring scene.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the document instance that owns this result.</summary>
    public SceneDocumentMetadata Metadata { get; } = metadata;

    /// <summary>Gets the accepted runtime activation.</summary>
    public RuntimeSceneTarget Target { get; } = target;
}
