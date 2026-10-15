// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

/// <summary>
/// The kind of an asset, as every editor surface presents it.
/// </summary>
public enum AssetKind
{
    /// <summary>
    /// Folder row.
    /// </summary>
    Folder,

    /// <summary>
    /// Material descriptor or cooked material.
    /// </summary>
    Material,

    /// <summary>
    /// Geometry descriptor or cooked geometry.
    /// </summary>
    Geometry,

    /// <summary>
    /// Scene descriptor or cooked scene.
    /// </summary>
    Scene,

    /// <summary>
    /// Texture descriptor or cooked texture.
    /// </summary>
    Texture,

    /// <summary>
    /// Image source file.
    /// </summary>
    Image,

    /// <summary>
    /// Import-settings descriptor.
    /// </summary>
    ImportSettings,

    /// <summary>
    /// Foreign source asset.
    /// </summary>
    ForeignSource,

    /// <summary>
    /// Cooked binary data file.
    /// </summary>
    CookedData,

    /// <summary>
    /// Cooked lookup table file.
    /// </summary>
    CookedTable,

    /// <summary>
    /// Unknown asset kind.
    /// </summary>
    Unknown,

    /// <summary>
    /// Script descriptor or cooked script.
    /// </summary>
    Script,

    /// <summary>
    /// Input-action descriptor or cooked input action.
    /// </summary>
    InputAction,

    /// <summary>
    /// Input-mapping-context descriptor or cooked mapping context.
    /// </summary>
    InputMappingContext,

    /// <summary>
    /// Physics-scene descriptor or cooked physics scene.
    /// </summary>
    PhysicsScene,

    /// <summary>
    /// Hand-written input file (<c>*.input.json</c> or <c>*.input-action.json</c>) that declares input actions and
    /// mapping contexts; each cooks to its own <see cref="InputAction"/> or <see cref="InputMappingContext"/>.
    /// </summary>
    InputFile,
}
