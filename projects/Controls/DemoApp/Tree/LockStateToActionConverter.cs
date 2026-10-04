// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Returns the action that changes an entity's current lock state.</summary>
internal sealed partial class LockStateToActionConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is true ? "Unlock entity" : "Lock entity";

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException("Lock action text conversion is one-way.");
}
