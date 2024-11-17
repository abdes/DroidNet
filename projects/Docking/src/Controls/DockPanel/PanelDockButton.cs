// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A styled <see cref="PanelButton" /> used for the docking button of a dock panel.
/// </summary>
internal sealed partial class PanelDockButton : PanelButton
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PanelDockButton"/> class.
    /// </summary>
    public PanelDockButton()
    {
        this.Style = (Style)Application.Current.Resources[nameof(PanelDockButton)];
    }
}
