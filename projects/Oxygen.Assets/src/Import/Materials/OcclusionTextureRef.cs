// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// A reference to a source texture used by a material as an occlusion map.
/// </summary>
/// <param name="Source">An <c>asset:///</c> URI referencing an authoring texture source (e.g. <c>.png</c>).</param>
/// <param name="Strength">Texture-specific strength factor (e.g. occlusion strength).</param>
public readonly record struct OcclusionTextureRef(string Source, float Strength);
