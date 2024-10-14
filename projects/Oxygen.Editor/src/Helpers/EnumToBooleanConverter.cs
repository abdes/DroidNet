// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Helpers;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// Checks if the string representation of the given parameter corresponds a given enum value.
/// </summary>
public partial class EnumToBooleanConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        // TODO: be more lenient and use the parameter.ToString()
        if (parameter is string enumString)
        {
            if (!Enum.IsDefined(typeof(ElementTheme), value))
            {
                throw new ArgumentException("ExceptionEnumToBooleanConverterValueMustBeAnEnum", nameof(value));
            }

            var enumValue = Enum.Parse(typeof(ElementTheme), enumString);

            return enumValue.Equals(value);
        }

        throw new ArgumentException("ExceptionEnumToBooleanConverterParameterMustBeAnEnumName", nameof(parameter));
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => DependencyProperty.UnsetValue;
}
