// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Transform vector groups edited by <see cref="TransformViewModel"/>.
/// </summary>
public enum TransformEditFieldGroup
{
    /// <summary>
    ///     Local position.
    /// </summary>
    Position,

    /// <summary>
    ///     Local rotation in Euler degrees.
    /// </summary>
    Rotation,

    /// <summary>
    ///     Local scale.
    /// </summary>
    Scale,
}
