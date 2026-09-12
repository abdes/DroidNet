// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A linear RGBA color passed through the managed runtime boundary.</summary>
/// <param name="R">The red component.</param>
/// <param name="G">The green component.</param>
/// <param name="B">The blue component.</param>
/// <param name="A">The alpha component.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeColor(float R, float G, float B, float A);
