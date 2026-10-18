// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>A scene to play.</summary>
/// <param name="DocumentId">The scene document.</param>
/// <param name="Scene">The scene, whose node order names the nodes the player reports.</param>
/// <param name="SceneAsset">The scene's asset identity, which is cooked and played.</param>
internal sealed record PlayRequest(DocumentId DocumentId, Scene Scene, AssetPath SceneAsset);
