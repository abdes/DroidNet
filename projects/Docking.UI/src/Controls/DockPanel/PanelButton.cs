// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

internal class PanelButton : Button
{
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph),
        typeof(string),
        typeof(PanelButton),
        new PropertyMetadata(string.Empty));

    public string Glyph
    {
        get => (string)this.GetValue(GlyphProperty);
        set => this.SetValue(GlyphProperty, value);
    }
}
