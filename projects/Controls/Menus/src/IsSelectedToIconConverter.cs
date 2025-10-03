// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls;

/// <summary>
/// Converts a boolean value indicating selection state to an icon representation.
/// </summary>
/// <remarks>
/// The <see cref="IsSelectedToIconConverter"/> class implements the <see cref="IValueConverter"/> interface to convert a boolean value to a <see cref="SymbolIcon"/>. If the value is <see langword="true"/>, it returns a checkmark icon; otherwise, it returns <see langword="null"/>.
/// </remarks>
public partial class IsSelectedToIconConverter : IValueConverter
{
    /// <inheritdoc/>
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        var isSelected = (bool)value;
        return isSelected ? new SymbolIcon(Symbol.Accept) : null;
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
