// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>A cube texture of the project's catalog.</summary>
/// <param name="Asset">The catalog item.</param>
/// <param name="StoresRadiance">Whether it keeps float texels and so can light a scene.</param>
internal readonly record struct CubeTextureAsset(ContentBrowserAssetItem Asset, bool StoresRadiance);
