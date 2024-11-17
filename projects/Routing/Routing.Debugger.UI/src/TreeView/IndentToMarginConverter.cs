// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// A converter that would provide the <see cref="Thickness">margin</see> corresponding to an item's depth in a tree.
/// </summary>
public partial class IndentToMarginConverter : IValueConverter
{
    /// <summary>
    /// Gets or sets the initial margin for the converter.
    /// </summary>
    public Thickness InitialMargin { get; set; } = new(0);

    /// <summary>
    /// Gets or sets the increment value for each indent level.
    /// </summary>
    public int IndentIncrement { get; set; } = 20;

    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language) => value is int indentLevel && targetType == typeof(Thickness)
            ? (object)new Thickness(
                this.InitialMargin.Left + (indentLevel * this.IndentIncrement),
                this.InitialMargin.Top,
                this.InitialMargin.Right,
                this.InitialMargin.Bottom)
            : throw new InvalidOperationException("Invalid types. Expected: (int, Thickness, Thickness).");

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
