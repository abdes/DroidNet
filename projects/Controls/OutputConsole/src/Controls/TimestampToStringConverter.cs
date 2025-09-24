// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Converts a <see cref="DateTimeOffset" /> timestamp to a formatted string.
///     If a format string is provided via the <c>parameter</c> argument, it will be
///     passed to <see cref="DateTimeOffset.ToString(string)" />; otherwise the default
///     representation is used.
/// </summary>
internal sealed partial class TimestampToStringConverter : IValueConverter
{
    /// <summary>
    ///     Converts a <see cref="DateTimeOffset" /> to a formatted string. If the provided value
    ///     is not a <see cref="DateTimeOffset" />, an empty string is returned.
    /// </summary>
    /// <param name="value">The source value; expected to be a <see cref="DateTimeOffset" />.</param>
    /// <param name="targetType">The type of the binding target property. (Ignored.)</param>
    /// <param name="parameter">An optional format string to pass to <see cref="DateTimeOffset.ToString(string)" />.</param>
    /// <param name="language">The culture to respect during conversion. (Ignored.)</param>
    /// <returns>
    ///     A formatted timestamp string, or an empty string when <paramref name="value" /> is not a
    ///     <see cref="DateTimeOffset" />.
    /// </returns>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is not DateTimeOffset dto)
        {
            return string.Empty;
        }

        var fmt = parameter as string;
        var provider = CultureInfo.CurrentCulture;
        return string.IsNullOrEmpty(fmt) ? dto.ToString(provider) : dto.ToString(fmt, provider);
    }

    /// <summary>
    ///     ConvertBack is not supported for timestamp formatting.
    /// </summary>
    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}
