// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Sets the scene's solid background through its native sky system.</summary>
/// <param name="Color">The scene-linear RGB value.</param>
public sealed record RuntimeSetBackgroundColor(Vector3 Color) : RuntimeWorldCommand;
