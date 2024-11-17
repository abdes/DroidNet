// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A custom styled button for use inside dock panels.
/// </summary>
/// <remarks>
/// The <see cref="PanelButton"/> class extends the <see cref="Button"/> class to provide a custom styled button specifically designed for use within dock panels.
/// It includes a <see cref="Glyph"/> property to display an icon or symbol on the button.
/// </remarks>
internal partial class PanelButton : Button
{
    /// <summary>
    /// Identifies the <see cref="Glyph"/> dependency property.
    /// </summary>
    /// <remarks>
    /// This field is used to register the <see cref="Glyph"/> property with the dependency property system.
    /// </remarks>
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph),
        typeof(string),
        typeof(PanelButton),
        new PropertyMetadata(string.Empty));

    /// <summary>
    /// Gets or sets the glyph (icon or symbol) displayed on the button.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the glyph to be displayed on the button.
    /// </value>
    /// <remarks>
    /// The <see cref="Glyph"/> property allows you to set an icon or symbol to be displayed on the button. This is useful for providing visual cues to users.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// <local:PanelButton Glyph="&#xE10F;" Content="Settings" />
    /// ]]></code>
    /// </para>
    /// </remarks>
    public string Glyph
    {
        get => (string)this.GetValue(GlyphProperty);
        set => this.SetValue(GlyphProperty, value);
    }
}
