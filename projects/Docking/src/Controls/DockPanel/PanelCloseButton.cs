// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A styled <see cref="PanelButton" /> used for the close button of a dock panel.
/// </summary>
internal sealed partial class PanelCloseButton : PanelButton
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PanelCloseButton"/> class.
    /// </summary>
    public PanelCloseButton()
    {
        this.Style = (Style)Application.Current.Resources[nameof(PanelCloseButton)];
    }
}
