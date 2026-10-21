// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;

namespace Oxygen.Editor.SceneInspector.Environment;

/// <summary>A cube texture of the project's catalog.</summary>
/// <param name="Asset">The catalog item.</param>
/// <param name="StoresRadiance">Whether it keeps float texels and so can light a scene.</param>
internal readonly record struct CubeTextureAsset(AssetEntry Asset, bool StoresRadiance);
