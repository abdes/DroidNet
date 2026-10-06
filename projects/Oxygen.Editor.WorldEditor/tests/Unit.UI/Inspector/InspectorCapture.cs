// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

internal static class InspectorCapture
{
    internal static async Task SaveIfRequestedAsync(FrameworkElement element, string name)
    {
        var directory = Environment.GetEnvironmentVariable("OXYGEN_UI_CAPTURE_DIRECTORY");
        if (string.IsNullOrWhiteSpace(directory))
        {
            return;
        }

        _ = Directory.CreateDirectory(directory);
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(element);
        var pixels = await bitmap.GetPixelsAsync();
        await using var stream = File.Create(Path.Combine(directory, $"{name}.png"));
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream.AsRandomAccessStream());
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied,
            (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels.ToArray());
        await encoder.FlushAsync();
    }

    internal static async Task HoldIfRequestedAsync(CancellationToken cancellationToken)
    {
        if (int.TryParse(Environment.GetEnvironmentVariable("OXYGEN_UI_CAPTURE_HOLD_SECONDS"), out var holdSeconds) && holdSeconds > 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(holdSeconds), cancellationToken);
        }
    }
}
