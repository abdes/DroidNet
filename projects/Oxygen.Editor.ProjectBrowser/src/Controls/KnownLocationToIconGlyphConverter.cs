// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ProjectBrowser.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Converts a <see cref="KnownLocations"/> value to a corresponding icon glyph.
/// </summary>
internal sealed partial class KnownLocationToIconGlyphConverter : IValueConverter
{
    /// <summary>
    /// Converts a <see cref="KnownLocations"/> value to a corresponding icon glyph.
    /// </summary>
    /// <param name="value">The value to convert. Must be of type <see cref="KnownLocations"/>.</param>
    /// <param name="targetType">The type of the binding target property. This parameter is not used.</param>
    /// <param name="parameter">The converter parameter to use. This parameter is not used.</param>
    /// <param name="language">The language of the conversion. This parameter is not used.</param>
    /// <returns>A string representing the icon glyph corresponding to the <see cref="KnownLocations"/> value.</returns>
    /// <exception cref="ArgumentException">Thrown when the value is not of type <see cref="KnownLocations"/>.</exception>
    /// <exception cref="InvalidEnumArgumentException">Thrown when the value is not a valid <see cref="KnownLocations"/>.</exception>
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is not KnownLocations knownLocation
            ? throw new ArgumentException("The value must be a KnownLocation.", nameof(value))
            : knownLocation switch
            {
                KnownLocations.RecentProjects => "\uE823",
                KnownLocations.Documents => "\uEC25",
                KnownLocations.Desktop => "\uE7F4",
                KnownLocations.Downloads => "\uEC27",
                KnownLocations.ThisComputer => "\uEC4E",
                KnownLocations.OneDrive => "\uE8F7",
                _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(KnownLocations)),
            };

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
