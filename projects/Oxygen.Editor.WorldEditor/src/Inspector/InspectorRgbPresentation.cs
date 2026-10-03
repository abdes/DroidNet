// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using DroidNet.Controls;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;
using Color = Windows.UI.Color;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns RGB display conversion and channel presentation for Inspector color editors.</summary>
internal static class InspectorRgbPresentation
{
    /// <summary>Converts an authored linear RGB value to an opaque sRGB display color.</summary>
    /// <param name="color">The linear RGB authoring value.</param>
    /// <returns>The sRGB display color.</returns>
    public static Color ToDisplayColor(Vector3 color) => ToDisplayColor(color.X, color.Y, color.Z);

    /// <summary>Creates a view-owned swatch brush for an authored linear RGB value.</summary>
    /// <param name="color">The linear RGB authoring value.</param>
    /// <returns>The display-converted swatch brush.</returns>
    public static SolidColorBrush ToBrush(Vector3 color) => new(ToDisplayColor(color));

    /// <summary>Converts linear authoring channels to the display-space picker and swatch color.</summary>
    /// <param name="red">The linear red channel.</param>
    /// <param name="green">The linear green channel.</param>
    /// <param name="blue">The linear blue channel.</param>
    /// <returns>The opaque sRGB display color.</returns>
    public static Color ToDisplayColor(float red, float green, float blue)
        => Color.FromArgb(255, ToSrgbByte(red), ToSrgbByte(green), ToSrgbByte(blue));

    /// <summary>Converts the display-space picker color to linear authoring channels.</summary>
    /// <param name="color">The selected sRGB display color.</param>
    /// <returns>The linear RGB authoring value.</returns>
    public static Vector3 ToLinearRgb(Color color)
        => new(FromSrgbByte(color.R), FromSrgbByte(color.G), FromSrgbByte(color.B));

    /// <summary>Configures the native compact component labels and their channel colors.</summary>
    /// <param name="vector">The RGB editor.</param>
    public static void Configure(VectorBox vector)
    {
        vector.ComponentLabels["X"] = "R";
        vector.ComponentLabels["Y"] = "G";
        vector.ComponentLabels["Z"] = "B";
        vector.ComponentLabelForegrounds["X"] = new SolidColorBrush(Colors.Red);
        vector.ComponentLabelForegrounds["Y"] = new SolidColorBrush(Colors.Green);
        vector.ComponentLabelForegrounds["Z"] = new SolidColorBrush(Colors.Blue);
    }

    private static byte ToSrgbByte(float value)
    {
        var linear = Math.Clamp(value, 0f, 1f);
        var encoded = linear <= 0.0031308f ? linear * 12.92f : (1.055f * MathF.Pow(linear, 1f / 2.4f)) - 0.055f;
        return (byte)Math.Clamp(MathF.Round(encoded * 255f), 0f, 255f);
    }

    private static float FromSrgbByte(byte value)
    {
        var encoded = value / 255f;
        return encoded <= 0.04045f ? encoded / 12.92f : MathF.Pow((encoded + 0.055f) / 1.055f, 2.4f);
    }
}
