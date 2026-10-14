// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>How the red, green and blue channels of a <see cref="ColorBox"/> are stored.</summary>
/// <remarks>The swatch and picker always show display (sRGB) colors; alpha is never converted.</remarks>
public enum ColorEncoding
{
    /// <summary>The channels are sRGB-encoded display values, shown as they are.</summary>
    Srgb,

    /// <summary>The channels are linear values, encoded to sRGB for display.</summary>
    Linear,
}
