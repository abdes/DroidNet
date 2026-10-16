// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Messages;

/// <summary>Publishes loaded authoring data independently of runtime availability.</summary>
/// <param name="scene">The loaded authoring scene.</param>
/// <param name="document">The scene document that holds the scene.</param>
internal sealed class SceneAuthoringLoadedMessage(Scene scene, Document document)
{
    /// <summary>Gets the loaded authoring scene.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the scene document that holds the scene.</summary>
    public Document Document { get; } = document;
}
