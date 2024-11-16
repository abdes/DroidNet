// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using Microsoft.UI.Xaml.Data;

/// <summary>
/// A converter to get the icon to be used for an expander based on whether it is expanded or not.
/// </summary>
public partial class ExpanderIconConverter : IValueConverter
{
    private const string GlyphChevronDown = "\uE972";
    private const string GlyphChevronUp = "\uE70E";

    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is bool)
        {
            return value is false ? GlyphChevronDown : GlyphChevronUp;
        }

        throw new ArgumentException(
            $"{nameof(ExpanderIconConverter)} can only convert bool values specifying whether the item is expanded or not",
            nameof(value));
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
