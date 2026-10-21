// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.SceneEditor.Play;

/// <summary>Maps a player window mode to the index of its segment: Windowed, then Borderless.</summary>
public sealed partial class PlayerWindowModeIndexConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is PlayerWindowMode.Borderless ? 1 : 0;

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value is 1 ? PlayerWindowMode.Borderless : PlayerWindowMode.Windowed;
}
