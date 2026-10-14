// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents a component of a vector (X, Y, Z or W). The value is the component's index in the vector.
/// </summary>
public enum Component
{
    /// <summary>
    ///     The X component.
    /// </summary>
    X = 0,

    /// <summary>
    ///     The Y component.
    /// </summary>
    Y = 1,

    /// <summary>
    ///     The Z component.
    /// </summary>
    Z = 2,

    /// <summary>
    ///     The W component, such as a quaternion's scalar part or a color's alpha.
    /// </summary>
    W = 3,
}
