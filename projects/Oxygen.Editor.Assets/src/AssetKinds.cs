// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Assets;

/// <summary>Classifies files and cooked content by the asset kinds the editor presents.</summary>
public static class AssetKinds
{
    /// <summary>Maps a native cooked asset type to the editor's asset kind.</summary>
    /// <param name="assetType">The native type identifier.</param>
    /// <returns>The asset kind, or <see cref="AssetKind.Unknown"/> for unsupported types.</returns>
    public static AssetKind FromCookedType(byte assetType) => (AssetType)assetType switch
    {
        AssetType.Material => AssetKind.Material,
        AssetType.Geometry => AssetKind.Geometry,
        AssetType.Scene => AssetKind.Scene,
        AssetType.Script => AssetKind.Script,
        AssetType.InputAction => AssetKind.InputAction,
        AssetType.InputMappingContext => AssetKind.InputMappingContext,
        AssetType.PhysicsScene => AssetKind.PhysicsScene,
        _ => AssetKind.Unknown,
    };

    /// <summary>Maps a file to the editor's asset kind from its file type.</summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The asset kind, or <see cref="AssetKind.Unknown"/> for a file the pipeline does not know.</returns>
    public static AssetKind FromFileName(string path) => AssetFileTypes.Find(path) switch
    {
        null => AssetKind.Unknown,
        { Category: AssetFileCategory.Script } => AssetKind.Script,
        { Category: AssetFileCategory.Input } => AssetKind.InputFile,
        { Category: AssetFileCategory.Image } => AssetKind.Image,
        { Category: AssetFileCategory.Model } => AssetKind.ForeignSource,
        { Category: AssetFileCategory.ImportSettings } => AssetKind.ImportSettings,
        { Category: AssetFileCategory.CookedResource } type => type.Suffix.Equals(".table", StringComparison.OrdinalIgnoreCase) ? AssetKind.CookedTable : AssetKind.CookedData,
        { CookedExtension: AssetFileTypes.Texture } => AssetKind.Texture,
        { AssetType: var assetType } => FromCookedType((byte)assetType),
    };
}
