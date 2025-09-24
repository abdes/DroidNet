// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Converts a boolean value to a <see cref="TextWrapping" /> value.
///     When the source is <see langword="true" /> this converter returns <see cref="TextWrapping.Wrap" />;
///     otherwise it returns <see cref="TextWrapping.NoWrap" />.
/// </summary>
internal sealed partial class BoolToTextWrappingConverter : IValueConverter
{
    /// <summary>
    ///     Converts a boolean to <see cref="TextWrapping" />.
    /// </summary>
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is true ? TextWrapping.Wrap : TextWrapping.NoWrap;

    /// <summary>
    ///     Not supported for this converter.
    /// </summary>
    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}
