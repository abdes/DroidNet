// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using System.Security.Cryptography;
using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;
using SceneNodeAdapter = Oxygen.Editor.WorldEditor.SceneExplorer.SceneNodeAdapter;

namespace Oxygen.Editor.WorldEditor.Utils;

/// <summary>
///     Provides methods for generating thumbnails for entities in the project explorer.
/// </summary>
public static class ThumbnailGenerator
{
    /// <summary>
    ///     Generates a random image with the specified width and height.
    /// </summary>
    /// <param name="width">The width of the image.</param>
    /// <param name="height">The height of the image.</param>
    /// <returns>A <see cref="WriteableBitmap" /> containing the generated image.</returns>
    public static WriteableBitmap GenerateRandomImage(int width, int height)
    {
        var bitmap = new WriteableBitmap(width, height);

        using var stream = bitmap.PixelBuffer.AsStream();
        var pixels = new byte[width * height * 4]; // RGBA

        RandomNumberGenerator.Fill(pixels);

        for (var i = 0; i < pixels.Length; i += 4)
        {
            pixels[i + 3] = 255; // A
        }

        stream.Write(pixels, 0, pixels.Length);

        return bitmap;
    }

    /// <summary>
    ///     Gets the thumbnail symbol for the specified entity.
    /// </summary>
    /// <param name="adapter">The tree item adapter representing the entity.</param>
    /// <returns>A <see cref="Symbol" /> representing the thumbnail for the entity.</returns>
    public static Symbol GetThumbnailForEntity(TreeItemAdapter adapter)
        => adapter is SceneNodeAdapter entityAdapter
           && entityAdapter.AttachedObject.Name.EndsWith('1')
            ? Symbol.Camera
            : Symbol.Calculator;
}
