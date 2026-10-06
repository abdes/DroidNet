// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>A scene source whose persisted name or reference paths changed.</summary>
/// <param name="SceneId">The stable authored scene identity.</param>
/// <param name="Previous">The source before the operation.</param>
/// <param name="Current">The source after the operation.</param>
public sealed record SceneAssetSourceChange(Guid SceneId, SceneSourceVersion Previous, SceneSourceVersion Current);
