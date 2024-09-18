// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;

public static class ThumbnailGenerator
{
    public static WriteableBitmap GenerateRandomImage(int width, int height)
    {
        var bitmap = new WriteableBitmap(width, height);

        using (var stream = bitmap.PixelBuffer.AsStream())
        {
            var pixels = new byte[width * height * 4]; // RGBA
            var rand = new Random();

            for (var i = 0; i < pixels.Length; i += 4)
            {
                pixels[i] = (byte)rand.Next(256); // R
                pixels[i + 1] = (byte)rand.Next(256); // G
                pixels[i + 2] = (byte)rand.Next(256); // B
                pixels[i + 3] = 255; // A
            }

            stream.Write(pixels, 0, pixels.Length);
        }

        return bitmap;
    }

    public static Symbol GetThumbnailForEntity() => Symbol.Admin;
}
