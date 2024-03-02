// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Globalization;

/// <summary>
/// Represents a dock identifier, with value semantics and immutability. The underlying value type is a <see langword="uint" />.
/// </summary>
/// <remarks>
/// The <see cref="DockId" /> type is often used with value semantics, is immutable once created, and can be used to as key type
/// in dictionary collections. The choice of a <see langword="record" /> <see langword="struct" /> for the implementation is the
/// most adapted for such usage scenarios. Additionally, using a custom type provides better quality of life when passing dock IDs
/// as arguments to methods (not to be confused with integer values).
/// </remarks>
/// <param name="Value">The unsigned integer value of the ID.</param>
public readonly record struct DockId(uint Value)
{
    /// <inheritdoc />
    public override string ToString() => this.Value.ToString(CultureInfo.InvariantCulture);
}
