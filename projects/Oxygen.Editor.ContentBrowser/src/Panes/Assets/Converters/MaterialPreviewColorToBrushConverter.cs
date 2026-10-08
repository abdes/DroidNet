// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.ContentBrowser.Materials;
using Windows.UI;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Converters;

/// <summary>Converts a material's base colour to an opaque swatch brush, mapped like the Material Editor's preview.</summary>
public sealed partial class MaterialPreviewColorToBrushConverter : IValueConverter
{
    /// <inheritdoc />
    public object? Convert(object value, Type targetType, object parameter, string language)
        => value is MaterialPreviewColor color
            ? new SolidColorBrush(Color.FromArgb(0xFF, ToByte(color.R), ToByte(color.G), ToByte(color.B)))
            : null;

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();

    private static byte ToByte(float value)
        => (byte)Math.Round(Math.Clamp(float.IsFinite(value) ? value : 0F, 0F, 1F) * 255F);
}
