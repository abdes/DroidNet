// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>Represents an outlet name.</summary>
/// <remarks>
/// This type offers a self-documented abstraction of outlet names,while keeping
/// it easy to convert from and to a string. It also encapsulates special
/// values, such as <see cref="Primary" />, which should always be used instead
/// of the actual literal string.
/// </remarks>
public record OutletName
{
    /// <summary>Gets the outlet name.</summary>
    /// <value>The string representation of the outlet name.</value>
    public required string Name { get; init; }

    /// <summary>
    /// Gets a special outlet name, used to refer to the primary outlet.
    /// </summary>
    /// <value>The primary outlet name.</value>
    public static OutletName Primary => string.Empty;

    /// <summary>
    /// Gets a value indicating whether the outlet name is for a primary outlet.
    /// </summary>
    /// <value>
    /// When <c>true</c>, the outlet name refers to a primary outlet.
    /// </value>
    public bool IsPrimary => this.Name.Equals(Primary.Name, StringComparison.Ordinal);

    /// <summary>
    /// Gets a value indicating whether the outlet name is <b>not</b> for a
    /// primary outlet.
    /// </summary>
    /// <value>
    /// When <c>true</c>, the outlet name refers to a non-primary outlet.
    /// </value>
    public bool IsNotPrimary => !this.IsPrimary;

    /// <summary>Implicitly convert a string to an OutletName.</summary>
    /// <param name="name">The outlet name as a string.</param>
    public static implicit operator OutletName(string? name) => name is null ? Primary : new OutletName { Name = name };

    /// <summary>Implicitly convert an OutletName to a string.</summary>
    /// <param name="source">An OutletName object.</param>
    public static implicit operator string(OutletName source) => source.Name;
}
