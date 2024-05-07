// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Converters;

using Microsoft.UI.Xaml.Data;

/// <summary>
/// Converts a binding to a dictionary with a string key as a parameter to the value corresponding to that key in the dictionary.
/// </summary>
/// <typeparam name="T">The type of values in the dictionary.</typeparam>
/// <remarks>
/// <para>
/// Only <see langword="string"/> keys are supported and it should not be an issue as only string values are really convenient to
/// use as keys in a binding.
/// </para>
/// <para>
/// As you cannot use a generic parameter in a static resource for defining the converter unless you are using XAML 2009, which is
/// not the case for WinUI as of now, we define a derived class for each value type we need to use with this converter. For
/// example, if the dictionary holds values of type <c>MyValueType</c>, then simply create a derived class
/// <c>MyValueTypeByKeyConverter</c> that derived from <c>DictionaryValueConverter&lt;MyValueType&gt;</c>. You don't need to
/// override any method.
/// </para>
/// </remarks>
public class DictionaryValueConverter<T> : IValueConverter
{
    /// <summary>
    /// Convert a binding to a dictionary item by key to the corresponding item.
    /// </summary>
    /// <param name="value">The dictionary; which must have a type compatible with <c>IDictionary&lt;string,T&gt;</c>.</param>
    /// <param name="targetType">Not used. The conversion expects the item type to be part of the dictionary type.</param>
    /// <param name="parameter">The key that will be used to get the returned item from the dictionary.</param>
    /// <param name="language">Not used. The item is returned as-is.</param>
    /// <returns>
    /// A non-null object of type <typeparamref name="T"/>if <paramref name="value"/> is not null, <paramref name="parameter"/> is
    /// not null and an item for the <paramref name="parameter"/> key exists in the dictionary; null otherwise.
    /// </returns>
    public object? Convert(object? value, Type targetType, object? parameter, string language)
    {
        if (value == null || parameter == null || value is not IDictionary<string, T> dictionary ||
            parameter is not string key)
        {
            return null;
        }

        return dictionary.TryGetValue(key, out var item) ? item! : null;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException("Don't use DictionaryValueConverter.ConvertBack; it's meaningless.");
}
