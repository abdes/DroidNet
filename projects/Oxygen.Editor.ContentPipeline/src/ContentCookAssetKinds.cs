// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Maps a file to the kind of cook input it is, from the pipeline's file types.</summary>
internal static class ContentCookAssetKinds
{
    /// <summary>
    /// Gets the cook kind of an authored descriptor or its cooked asset (texture, material, geometry, scene), a script
    /// source or its cooked script, a model or an input file.
    /// </summary>
    /// <param name="path">A file name, path or virtual path.</param>
    /// <returns>The kind, or <see cref="ContentCookAssetKind.Unknown"/> for any other file.</returns>
    public static ContentCookAssetKind Of(string path)
        => AssetFileTypes.Find(path) switch
        {
            { Category: AssetFileCategory.Model } => ContentCookAssetKind.ForeignSource,
            { Category: AssetFileCategory.Input } => ContentCookAssetKind.Input,
            { Category: AssetFileCategory.Script or AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Script } => ContentCookAssetKind.Script,
            { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Texture } => ContentCookAssetKind.Texture,
            { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Material } => ContentCookAssetKind.Material,
            { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Geometry } => ContentCookAssetKind.Geometry,
            { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Scene } => ContentCookAssetKind.Scene,
            _ => ContentCookAssetKind.Unknown,
        };
}
