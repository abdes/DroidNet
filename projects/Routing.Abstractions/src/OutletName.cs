// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using Destructurama.Attributed;

/// <summary>
/// Represents an outlet name, providing abstraction and easy conversion to and from strings.
/// Special values like <see cref="Primary" /> should be used for specific purposes.
/// </summary>
[LogAsScalar]
public record OutletName
{
    /// <summary>
    /// Gets the outlet name as a string.
    /// </summary>
    public required string Name { get; init; }

    /// <summary>
    /// Gets the special outlet name for the primary outlet.
    /// </summary>
    public static OutletName Primary => new(string.Empty);

    /// <summary>
    /// Gets a value indicating whether the outlet name is for the primary outlet.
    /// </summary>
    public bool IsPrimary => this.Name.Equals(Primary.Name, StringComparison.Ordinal);

    /// <summary>
    /// Gets a value indicating whether the outlet name is not for the primary outlet.
    /// </summary>
    public bool IsNotPrimary => !this.IsPrimary;

    /// <summary>
    /// Implicitly converts a string to an OutletName. If the string is null, returns <see cref="Primary" />.
    /// </summary>
    /// <param name="name">The outlet name as a string.</param>
    public static implicit operator OutletName(string? name) => name is null ? Primary : new OutletName { Name = name };

    /// <summary>
    /// Implicitly converts an OutletName to a string.
    /// </summary>
    /// <param name="source">The OutletName object.</param>
    public static implicit operator string(OutletName source) => source.Name;

    public override string ToString() => this.Name;

    /// <summary>
    /// Custom equality comparer for <see cref="OutletName" />, suitable for use with collections.
    /// Compares outlet names using ordinal (binary) sort rules, with optional case insensitivity.
    /// </summary>
    /// <param name="ignoreCase">When true, ignores the case of the outlet names. Default is true.</param>
    public class EqualityComparer(bool ignoreCase = true) : EqualityComparer<OutletName>
    {
        /// <summary>
        /// Default comparer instance that ignores case.
        /// </summary>
        public static readonly EqualityComparer IgnoreCase = new();

        /// <inheritdoc />
        public override bool Equals(OutletName? x, OutletName? y)
        {
            if (ReferenceEquals(x, y))
            {
                return true;
            }

            if (x is null || y is null)
            {
                return false;
            }

            return x.Name.Equals(y.Name, ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
        }

        /// <inheritdoc />
        public override int GetHashCode(OutletName obj) => obj.Name.GetHashCode(StringComparison.Ordinal);
    }
}
