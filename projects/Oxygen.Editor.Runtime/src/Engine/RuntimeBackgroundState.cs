// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Native scene background state; it does not establish visible presentation.</summary>
/// <param name="Exists">Whether the scene has an enabled presentation background.</param>
/// <param name="Color">The stored scene-linear RGB value.</param>
/// <param name="AtmosphereEnabled">Whether atmosphere currently takes precedence over the background.</param>
public sealed record RuntimeBackgroundState(bool Exists, Vector3 Color, bool AtmosphereEnabled);
