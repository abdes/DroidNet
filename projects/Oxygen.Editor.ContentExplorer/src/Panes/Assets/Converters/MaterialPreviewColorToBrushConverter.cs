// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Assets.Materials;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Converters;

/// <summary>
/// Converts a material's base colour to an opaque swatch brush. The colour is stored linear; it is shown as the
/// Material Editor shows it, converted to the display's encoding.
/// </summary>
public sealed partial class MaterialPreviewColorToBrushConverter : IValueConverter
{
    /// <inheritdoc />
    public object? Convert(object value, Type targetType, object parameter, string language)
        => value is MaterialPreviewColor color
            ? new SolidColorBrush(ColorPresentation.ToDisplayColor(new ColorChannels(color.R, color.G, color.B, Alpha: 1), ColorEncoding.Linear, ColorRange.Normalized))
            : null;

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
