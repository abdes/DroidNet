// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>The range of the red, green and blue channels of a <see cref="ColorBox"/>.</summary>
public enum ColorRange
{
    /// <summary>Channels range from 0 to 1.</summary>
    Normalized,

    /// <summary>
    ///     Channels may exceed 1. The swatch and picker show the hue with the brightest channel scaled down to 1, and a
    ///     pick keeps that brightness.
    /// </summary>
    HighDynamicRange,
}
