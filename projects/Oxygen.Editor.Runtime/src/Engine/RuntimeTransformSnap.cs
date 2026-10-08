// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Increments a snapped gizmo drag rounds to.</summary>
/// <param name="Enabled">Whether drags snap; Ctrl inverts it for a drag.</param>
/// <param name="Translation">Metres.</param>
/// <param name="RotationDegrees">Degrees.</param>
/// <param name="Scale">Scale factor step.</param>
public readonly record struct RuntimeTransformSnap(bool Enabled, float Translation, float RotationDegrees, float Scale);
