// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Maps an item's locked state to its Segoe Fluent lock/unlock glyph.</summary>
internal sealed partial class LockStateToGlyphConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is true ? "\uE72E" : "\uE785";

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException("Lock glyph conversion is one-way.");
}
