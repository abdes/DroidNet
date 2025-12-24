// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// A reference to a source texture used by a material.
/// </summary>
/// <param name="Source">An <c>asset:///</c> URI referencing an authoring texture source (e.g. <c>.png</c>).</param>
public readonly record struct MaterialTextureRef(string Source);
