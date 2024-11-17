// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Converters;

/// <summary>
/// A converter that will convert an <see cref="ItemClickEventArgs" /> object into the corresponding clicked item object.
/// </summary>
public partial class ItemClickEventArgsToClickedItemConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object? parameter, string language)
        => value is ItemClickEventArgs args
            ? args.ClickedItem
            : throw new ArgumentException(
                $"{nameof(ItemClickEventArgsToClickedItemConverter)} can only convert {nameof(ItemClickEventArgs)} values",
                nameof(value));

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException();
}
