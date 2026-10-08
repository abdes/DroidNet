// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A rectangle of a view's surface to pick, in physical pixels from its top-left corner.</summary>
/// <param name="X">The left edge.</param>
/// <param name="Y">The top edge.</param>
/// <param name="Width">The width; at least one pixel.</param>
/// <param name="Height">The height; at least one pixel.</param>
public readonly record struct RuntimePickRect(uint X, uint Y, uint Width, uint Height);
