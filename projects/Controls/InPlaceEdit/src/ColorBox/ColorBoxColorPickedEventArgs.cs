// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Carries a color picked in a <see cref="ColorBox"/> picker, as stored channels.</summary>
/// <param name="color">The picked red, green, blue and alpha channels, in the box's encoding and range.</param>
public sealed class ColorBoxColorPickedEventArgs(ColorChannels color) : EventArgs
{
    /// <summary>Gets the picked red, green, blue and alpha channels.</summary>
    public ColorChannels Color { get; } = color;

    /// <summary>
    ///     Gets or sets a value indicating whether the handler applied the color, for example as one edit to its model.
    ///     When not handled, the box sets its own channels.
    /// </summary>
    public bool Handled { get; set; }
}
