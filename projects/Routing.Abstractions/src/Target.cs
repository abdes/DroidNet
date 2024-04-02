// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>Represents a routing target.</summary>
/// <remarks>
/// This type offers a self-documented abstraction of a routing target,
/// identified by its name, while keeping it easy to convert from and to a
/// string. It also encapsulates special values, such as <see cref="Main" /> and
/// <see cref="Self" />, which should always be used instead of the actual
/// literal string.
/// </remarks>
public record Target
{
    /// <summary>Gets the target name.</summary>
    /// <value>The string representation of the target.</value>
    public required string Name { get; init; }

    /// <summary>
    /// Gets a special target, used to refer to the main top level target.
    /// </summary>
    /// <value>The main target name.</value>
    public static Target Main => "_main";

    /// <summary>
    /// Gets a special target, used to refer to the router context of the
    /// current <see cref="IActiveRoute" />.
    /// </summary>
    /// <remarks>
    /// When a target is not specified for a navigation request, it is assumed
    /// to be <see cref="Self" />.
    /// </remarks>
    /// <value>The target name for the current <see cref="IActiveRoute" />.</value>
    public static Target Self => "_self";

    /// <summary>
    /// Gets a value indicating whether the target is the <see cref="Self" />
    /// target.
    /// </summary>
    /// <value>
    /// When <see langword="true" />, the target refers to the <see cref="Self" /> target.
    /// </value>
    public bool IsSelf => this.Name.Equals(Self.Name, StringComparison.Ordinal);

    /// <summary>
    /// Gets a value indicating whether the target is the <see cref="Main" />
    /// target.
    /// </summary>
    /// <value>
    /// When <see langword="true" />, the target refers to the <see cref="Main" /> target.
    /// </value>
    public bool IsMain => this.Name.Equals(Main.Name, StringComparison.Ordinal);

    /// <summary>Implicitly convert a string to a Target.</summary>
    /// <param name="name">The target name as a string.</param>
    public static implicit operator Target(string? name) => name is null ? Self : new Target { Name = name };

    /// <summary>Implicitly convert a Target to a string.</summary>
    /// <param name="source">A Target object.</param>
    public static implicit operator string(Target source) => source.Name;
}
