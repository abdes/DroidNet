// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Controls;

/// <summary>What an authored RGB value means, which decides how an <see cref="ColorField"/> stores, shows and
/// labels it. Every kind is stored linear, as the engine uses it.</summary>
public enum ColorFieldKind
{
    /// <summary>A reflectance, albedo or tint, from 0 to 1.</summary>
    Linear,

    /// <summary>An emitted or scattered luminance, which may exceed 1. The swatch and picker show its hue; a pick keeps
    /// its brightness.</summary>
    Luminance,

    /// <summary>A per-channel scale applied to another color. It has no color of its own, so it has no picker.</summary>
    Multiplier,
}
