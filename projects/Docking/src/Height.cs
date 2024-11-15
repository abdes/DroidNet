// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Detail;

/// <summary>
/// A specialization of the <see cref="Length" /> type, representing the `height` of a dockable as a specific type that cannot
/// be confused with any other dimension.
/// </summary>
public class Height : Length
{
    /// <summary>
    /// Initializes a new instance of the <see cref="Height" /> class with a string value.
    /// </summary>
    /// <inheritdoc />
    public Height(string? value = null)
        : base(value)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="Height" /> class with a specific numeric value as pixels.
    /// </summary>
    /// <inheritdoc />
    public Height(double pixels)
        : base(pixels)
    {
    }

    /// <summary>
    /// Implicitly convert the underlying value of a <see cref="Height" /> to a string.
    /// </summary>
    /// <param name="length">the length to be converted to a string.</param>
    public static implicit operator string?(Height? length) => (Length?)length;

    /// <inheritdoc />
    public override string? ToString() => this;

    /// <inheritdoc />
    public override string ToDebugString() => "Height=" + base.ToDebugString();
}
