// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using Windows.UI.Text;

namespace DroidNet.Controls;

/// <summary>Measures the width of single-line text without measuring any element of a visual tree.</summary>
/// <remarks>
///     A control that sizes itself from its text, such as a value, a unit or a qualifier, asks for the width its text
///     takes in the typography of the template part that shows it. Measuring the part itself would change its layout
///     state outside its parent's layout pass. Widths are cached per UI thread by text, typography and system text
///     scale, so repeated layout passes measure nothing.
/// </remarks>
public static class TextMeasurer
{
    private const int CacheCapacity = 512;

    [ThreadStatic]
    private static Dictionary<TextKey, double>? cache;

    /// <summary>Gets the width the text takes on one line in the typography of a text block.</summary>
    /// <param name="text">The text; null or empty takes no width.</param>
    /// <param name="typography">The text block whose font family, size, weight, style, stretch, character spacing and
    /// text scaling the text uses.</param>
    /// <returns>The width in device-independent pixels, excluding the text block's padding and margin.</returns>
    public static double Width(string? text, TextBlock typography)
    {
        ArgumentNullException.ThrowIfNull(typography);
        if (string.IsNullOrEmpty(text))
        {
            return 0;
        }

        var key = new TextKey(
            text,
            typography.FontFamily?.Source ?? string.Empty,
            typography.FontSize,
            typography.FontWeight.Weight,
            typography.FontStyle,
            typography.FontStretch,
            typography.CharacterSpacing,
            typography.IsTextScaleFactorEnabled ? TextScale.Factor : 1d);
        cache ??= [];
        if (cache.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var probe = new TextBlock
        {
            Text = text,
            FontFamily = typography.FontFamily,
            FontSize = typography.FontSize,
            FontWeight = typography.FontWeight,
            FontStyle = typography.FontStyle,
            FontStretch = typography.FontStretch,
            CharacterSpacing = typography.CharacterSpacing,
            IsTextScaleFactorEnabled = typography.IsTextScaleFactorEnabled,
        };
        probe.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var measured = probe.DesiredSize.Width;
        if (cache.Count >= CacheCapacity)
        {
            cache.Clear();
        }

        cache[key] = measured;
        return measured;
    }

    private readonly record struct TextKey(
        string Text,
        string FontFamily,
        double FontSize,
        ushort FontWeight,
        FontStyle FontStyle,
        FontStretch FontStretch,
        int CharacterSpacing,
        double Scale);
}
