// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Layout builder utilities for <see cref="SceneAdapter" />. Provides an adapter tree that
/// separates layout nodes from scene nodes via <see cref="SceneNodeAdapter" /> wrappers.
/// </summary>
public partial class SceneAdapter
{
    /// <summary>
    /// Builds a layout adapter tree rooted at a new <see cref="SceneAdapter" /> using the scene's
    /// current <see cref="Scene.ExplorerLayout" />. Nodes are wrapped in <see cref="SceneNodeAdapter" />
    /// so layout operations remain scene-agnostic.
    /// </summary>
    /// <param name="scene">Scene backing the adapter tree.</param>
    /// <returns>Root <see cref="SceneAdapter" /> with layout-only children attached.</returns>
    public static SceneAdapter BuildLayoutTree(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var root = new SceneAdapter(scene)
        {
            IsExpanded = true,
            IsLocked = true,
            IsRoot = true,
            UseLayoutAdapters = true,
        };

        return root;
    }
}
