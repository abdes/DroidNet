// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Destructurama.Attributed;

namespace DroidNet.Routing;
/// <summary>
/// A strongly-typed representation of an outlet name that avoids confusion with regular strings and
/// provides consistent case handling.
/// </summary>
/// <remarks>
/// <para>
/// Rather than using raw strings that could be confused with other string values in the application,
/// OutletName provides a dedicated type for outlet identification. This type-safety helps prevent errors
/// where arbitrary strings might be mistakenly used as outlet names. By default, outlet names are
/// compared case-insensitively, ensuring consistent behavior across the routing system.
/// </para>
/// <para>
/// The class recognizes a special <see cref="Primary"/> outlet, represented by an empty string, which
/// serves as the default target for route content. Conversion operators make it convenient to work with
/// string-based APIs while maintaining type safety.
/// </para>
/// </remarks>
[LogAsScalar]
public record OutletName
{
    /// <summary>
    /// Gets the outlet name as a string.
    /// </summary>
    public required string Name { get; init; }

    /// <summary>
    /// Gets the special outlet name representing the primary outlet.
    /// </summary>
    /// <value>
    /// An <see cref="OutletName"/> instance with an empty string value, used as the default target
    /// for route content when no specific outlet is specified.
    /// </value>
    public static OutletName Primary => new(string.Empty);

    /// <summary>
    /// Gets a value indicating whether this outlet represents the primary outlet.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if this outlet has an empty name matching the <see cref="Primary"/> outlet;
    /// otherwise, <see langword="false"/>.
    /// </value>
    public bool IsPrimary => this.Name.Equals(Primary.Name, StringComparison.Ordinal);

    /// <summary>
    /// Gets a value indicating whether this outlet represents a secondary (non-primary) outlet.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if this outlet has a non-empty name; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsNotPrimary => !this.IsPrimary;

    /// <summary>
    /// Implicitly converts a string to an <see cref="OutletName"/>.
    /// </summary>
    /// <param name="name">The outlet name as a string.</param>
    /// <returns>
    /// For <see langword="null"/> input, returns the <see cref="Primary"/> outlet; otherwise, returns
    /// a new outlet with the specified name.
    /// </returns>
    public static implicit operator OutletName(string? name) => FromString(name);

    /// <summary>
    /// Implicitly converts an <see cref="OutletName"/> to its string representation.
    /// </summary>
    /// <param name="source">The outlet name to convert.</param>
    /// <returns>The underlying string value of the outlet name.</returns>
    public static implicit operator string(OutletName source) => source.Name;

    /// <summary>
    /// Creates an <see cref="OutletName"/> from a string value.
    /// </summary>
    /// <param name="name">The outlet name as a string.</param>
    /// <returns>
    /// For <see langword="null"/> input, returns the <see cref="Primary"/> outlet; otherwise, returns
    /// a new outlet with the specified name.
    /// </returns>
    public static OutletName FromString(string? name) => name is null ? Primary : new OutletName { Name = name };

    /// <inheritdoc/>
    public override string ToString() => this;
}

/// <summary>
/// Provides consistent equality comparison for <see cref="OutletName"/> instances, defaulting to
/// case-insensitive comparison.
/// </summary>
/// <remarks>
/// <para>
/// This comparer ensures that outlet names are matched consistently throughout the routing system,
/// particularly in collections and dictionaries. By default, it performs case-insensitive comparison
/// using ordinal rules, making "SidePanel" and "sidepanel" equivalent outlet names.
/// </para>
/// <para>
/// While case-insensitive comparison is the recommended approach, the comparer can be configured for
/// case-sensitive comparison when needed. The static <see cref="IgnoreCase"/> property provides a
/// shared instance of the default case-insensitive comparer.
/// </para>
/// </remarks>
/// <param name="ignoreCase">
/// When <see langword="true"/>, outlet names are compared ignoring case. Defaults to <see langword="true"/>.
/// </param>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "StyleCop.CSharp.MaintainabilityRules",
    "SA1402:File may only contain a single type",
    Justification = "The Equality Comparer and the OutletName are tightly coupled")]
public class OutletNameEqualityComparer(bool ignoreCase = true) : EqualityComparer<OutletName>
{
    /// <summary>
    /// Gets the default case-insensitive comparer instance.
    /// </summary>
    /// <value>
    /// A shared instance of <see cref="OutletNameEqualityComparer"/> configured for case-insensitive comparison.
    /// </value>
    public static readonly OutletNameEqualityComparer IgnoreCase = new();

    /// <summary>
    /// Determines whether two outlet names are equal.
    /// </summary>
    /// <param name="x">The first outlet name to compare.</param>
    /// <param name="y">The second outlet name to compare.</param>
    /// <returns>
    /// <see langword="true"/> if the outlet names are equal using the configured case sensitivity;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// Comparison uses ordinal rules with configurable case sensitivity. <see langword="null"/> values
    /// are handled according to standard equality comparison rules.
    /// </remarks>
    public override bool Equals(OutletName? x, OutletName? y)
        => ReferenceEquals(x, y)
        || (x is not null && y is not null &&
            x.Name.Equals(y.Name, ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal));

    /// <summary>
    /// Gets a hash code for the specified outlet name.
    /// </summary>
    /// <param name="obj">The outlet name.</param>
    /// <returns>A hash code that remains consistent regardless of case sensitivity settings.</returns>
    /// <remarks>
    /// Always uses ordinal comparison for hash code generation to ensure consistency across different
    /// comparer instances.
    /// </remarks>
    public override int GetHashCode(OutletName obj) =>
        obj.Name.GetHashCode(StringComparison.Ordinal);
}
