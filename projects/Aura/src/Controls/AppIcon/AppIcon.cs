// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents an application icon control for displaying app icons in the UI.
/// </summary>
internal partial class AppIcon : Control
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="AppIcon"/> class.
    /// </summary>
    public AppIcon()
    {
        this.DefaultStyleKey = typeof(AppIcon);
    }

    // TODO: implement system menu on right click
}
