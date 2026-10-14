// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.UI;

namespace DroidNet.Controls;

/// <summary>Converts stored color channels to and from the display colors a swatch and a color picker show.</summary>
/// <remarks>Alpha is straight and never converted.</remarks>
public static class ColorPresentation
{
    /// <summary>Encodes a linear value with the sRGB transfer function.</summary>
    /// <param name="linear">The linear value; clamped to 0 to 1.</param>
    /// <returns>The sRGB-encoded value.</returns>
    public static double LinearToSrgb(double linear)
    {
        var value = Math.Clamp(linear, 0d, 1d);
        return value <= 0.0031308 ? value * 12.92 : (1.055 * Math.Pow(value, 1d / 2.4)) - 0.055;
    }

    /// <summary>Decodes an sRGB-encoded value to linear.</summary>
    /// <param name="encoded">The sRGB-encoded value; clamped to 0 to 1.</param>
    /// <returns>The linear value.</returns>
    public static double SrgbToLinear(double encoded)
    {
        var value = Math.Clamp(encoded, 0d, 1d);
        return value <= 0.04045 ? value / 12.92 : Math.Pow((value + 0.055) / 1.055, 2.4);
    }

    /// <summary>Gets the brightness the display color is scaled by: the brightest channel for a high dynamic range
    /// color brighter than 1, otherwise 1.</summary>
    /// <param name="color">The stored channels.</param>
    /// <param name="range">The channel range.</param>
    /// <returns>The scale, at least 1.</returns>
    public static double Intensity(ColorChannels color, ColorRange range)
        => range == ColorRange.HighDynamicRange ? Math.Max(1d, Math.Max(color.Red, Math.Max(color.Green, color.Blue))) : 1d;

    /// <summary>Converts stored channels to the display color, at the brightness <see cref="Intensity"/> gives.</summary>
    /// <param name="color">The stored channels.</param>
    /// <param name="encoding">How the red, green and blue channels are stored.</param>
    /// <param name="range">The channel range.</param>
    /// <returns>The display color.</returns>
    public static Color ToDisplayColor(ColorChannels color, ColorEncoding encoding, ColorRange range)
        => ToDisplayColor(color, encoding, Intensity(color, range));

    /// <summary>Converts stored channels to the display color at a given brightness.</summary>
    /// <param name="color">The stored channels.</param>
    /// <param name="encoding">How the red, green and blue channels are stored.</param>
    /// <param name="intensity">The brightness the red, green and blue channels are divided by.</param>
    /// <returns>The display color.</returns>
    public static Color ToDisplayColor(ColorChannels color, ColorEncoding encoding, double intensity)
        => Color.FromArgb(
            ToByte(color.Alpha),
            ToByte(Encode(color.Red / intensity, encoding)),
            ToByte(Encode(color.Green / intensity, encoding)),
            ToByte(Encode(color.Blue / intensity, encoding)));

    /// <summary>Converts a display color to stored channels at a given brightness.</summary>
    /// <param name="color">The display color.</param>
    /// <param name="encoding">How the red, green and blue channels are stored.</param>
    /// <param name="intensity">The brightness the red, green and blue channels are multiplied by.</param>
    /// <returns>The stored channels.</returns>
    public static ColorChannels FromDisplayColor(Color color, ColorEncoding encoding, double intensity)
        => new(
            Decode(color.R / 255d, encoding) * intensity,
            Decode(color.G / 255d, encoding) * intensity,
            Decode(color.B / 255d, encoding) * intensity,
            color.A / 255d);

    /// <summary>Applies a picked display color to stored channels, changing only the channels whose displayed value
    /// changed, so a pick never rounds channels the user did not touch.</summary>
    /// <param name="current">The stored channels.</param>
    /// <param name="picked">The picked display color.</param>
    /// <param name="encoding">How the red, green and blue channels are stored.</param>
    /// <param name="intensity">The brightness of the pick.</param>
    /// <returns>The stored channels after the pick.</returns>
    public static ColorChannels ApplyPick(ColorChannels current, Color picked, ColorEncoding encoding, double intensity)
    {
        var shown = ToDisplayColor(current, encoding, intensity);
        var pickedChannels = FromDisplayColor(picked, encoding, intensity);
        return new(
            shown.R == picked.R ? current.Red : pickedChannels.Red,
            shown.G == picked.G ? current.Green : pickedChannels.Green,
            shown.B == picked.B ? current.Blue : pickedChannels.Blue,
            shown.A == picked.A ? current.Alpha : pickedChannels.Alpha);
    }

    private static double Encode(double value, ColorEncoding encoding)
        => encoding == ColorEncoding.Linear ? LinearToSrgb(value) : value;

    private static double Decode(double value, ColorEncoding encoding)
        => encoding == ColorEncoding.Linear ? SrgbToLinear(value) : value;

    private static byte ToByte(double value) => (byte)Math.Round(Math.Clamp(value, 0d, 1d) * 255d, MidpointRounding.AwayFromZero);
}
