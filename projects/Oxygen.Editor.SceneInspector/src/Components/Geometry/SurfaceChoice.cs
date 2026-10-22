// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Assets;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>One observed native surface; its label never establishes assignment identity.</summary>
/// <param name="Target">The geometry, opaque surface and observed inventory revision.</param>
/// <param name="DisplayName">The native presentation label.</param>
public sealed record SurfaceChoice(SurfaceTarget Target, string DisplayName);
