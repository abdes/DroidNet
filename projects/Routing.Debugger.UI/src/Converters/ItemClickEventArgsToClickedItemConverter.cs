// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Converters;

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

public class ItemClickEventArgsToClickedItemConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is ItemClickEventArgs args
            ? args.ClickedItem
            : throw new ArgumentException(
                $"{nameof(ItemClickEventArgsToClickedItemConverter)} can only convert {nameof(ItemClickEventArgs)} values",
                nameof(value));

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
