// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets;

/// <summary>
/// Provides constant values used throughout the Oxygen Assets module.
/// </summary>
public static class AssetPipelineConstants
{
    /// <summary>
    /// The name of the folder where cooked assets are stored.
    /// </summary>
    public const string CookedFolderName = ".cooked";

    /// <summary>
    /// The name of the folder where imported asset metadata is stored.
    /// </summary>
    public const string ImportedFolderName = ".imported";

    /// <summary>
    /// The name of the loose cooked index file.
    /// </summary>
    public const string IndexFileName = "container.index.bin";

    /// <summary>
    /// Extension for texture asset descriptors.
    /// </summary>
    public const string TextureExtension = ".otex";

    /// <summary>
    /// Extension for geometry asset descriptors.
    /// </summary>
    public const string GeometryExtension = ".ogeo";

    /// <summary>
    /// Extension for material asset descriptors.
    /// </summary>
    public const string MaterialExtension = ".omat";

    /// <summary>
    /// Extension for scene asset descriptors.
    /// </summary>
    public const string SceneExtension = ".oscene";

    /// <summary>
    /// Suffix for scene authoring source files (<c>*.oscene.json</c>).
    /// </summary>
    public const string SceneSourceSuffix = SceneExtension + GeneratedSourceExtension;

    /// <summary>
    /// Extension for generated source files (JSON).
    /// </summary>
    public const string GeneratedSourceExtension = ".json";

    /// <summary>
    /// Extension for GLB files (used for intermediate geometry).
    /// </summary>
    public const string GlbExtension = ".glb";

    /// <summary>
    /// Extension for PNG files (used for intermediate textures).
    /// </summary>
    public const string PngExtension = ".png";
}
