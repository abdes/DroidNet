// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Play;

/// <summary>A scene to play.</summary>
/// <param name="DocumentId">The scene document.</param>
/// <param name="Scene">The scene, whose node order names the nodes the player reports.</param>
/// <param name="SceneAssetUri">The scene's asset identity, which is cooked and played.</param>
public sealed record PlayRequest(DocumentId DocumentId, Scene Scene, Uri SceneAssetUri);
