// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Converts an item type to a corresponding <see cref="SymbolIcon"/> for display in the UI.
///     This implementation always returns the <see cref="Symbol.Audio"/> icon as a placeholder.
/// </summary>
public partial class ItemsTypeToIconConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language) =>
        new SymbolIcon { Symbol = Symbol.Audio };

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
