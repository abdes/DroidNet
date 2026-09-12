// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Shows the same scene-node glyph used by the scene hierarchy.
/// </summary>
public partial class ItemsTypeToIconConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language) =>
        new FontIcon { Glyph = "\uE7C1", FontSize = 16 };

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
