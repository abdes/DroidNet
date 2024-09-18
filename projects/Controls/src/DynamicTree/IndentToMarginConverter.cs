// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

public class IndentToMarginConverter : IValueConverter
{
    public Thickness InitialMargin { get; set; } = new(0);

    public int IndentIncrement { get; set; }

    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is int indentLevel && targetType == typeof(Thickness))
        {
            return new Thickness(
                this.InitialMargin.Left + (indentLevel * this.IndentIncrement),
                this.InitialMargin.Top,
                this.InitialMargin.Right,
                this.InitialMargin.Bottom);
        }

        throw new InvalidOperationException("Invalid types. Expected: (int, Thickness, Thickness).");
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
