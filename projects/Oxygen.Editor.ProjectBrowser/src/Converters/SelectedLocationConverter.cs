// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Converters;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ProjectBrowser.Projects;

/// <summary>
/// A converter used to determine whether an item containing a <see cref="KnownLocation" /> is selected or not by comparing the
/// currently selected location enum value to the its string representation passed as a paramter to the converter.
/// </summary>
public partial class SelectedLocationConverter : IValueConverter
{
    /// <summary>
    /// Determine whether an item containing a <see cref="KnownLocation" /> is selected or not by comparing the currently selected
    /// location enum <paramref name="value" /> to the its string representation passed as a <paramref name="parameter" /> to the
    /// converter.
    /// </summary>
    /// <param name="value">
    /// The value produced by the binding source, containing the currently selected <see cref="KnownLocation" />.
    /// </param>
    /// <param name="targetType">
    /// The type of the binding target property.
    /// </param>
    /// <param name="parameter">
    /// The converter parameter, which string representation will be checked the <see cref="KnownLocations" /> enum value.
    /// </param>
    /// <param name="language">The language of the conversion.</param>
    /// <returns>
    /// A boolean indicating whether the <paramref name="parameter" /> is the string representation of the
    /// <see cref="KnownLocations" /> enum value of the <see cref="KnownLocation.Key" /> of currently selected
    /// <see cref="KnownLocation" /> in <paramref name="value" />.
    /// </returns>
    public object Convert(object? value, Type targetType, object? parameter, string language)
    {
        if (value == null || parameter == null || value is not KnownLocation location)
        {
            return false;
        }

        if (!Enum.TryParse(typeof(KnownLocations), parameter.ToString(), ignoreCase: true, out var locationKey))
        {
            return false;
        }

        return (KnownLocations)locationKey == location.Key;
    }

    /// <inheritdoc />
    /// <remarks>
    /// Reverse conversion does not make sense and will not produce a binding value.
    /// </remarks>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => DependencyProperty.UnsetValue;
}
