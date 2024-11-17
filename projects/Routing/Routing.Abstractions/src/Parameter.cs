// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a single URL parameter with a name and optional value, used in both matrix parameters
/// and query parameters.
/// </summary>
/// <remarks>
/// <para>
/// A parameter consists of a name and an optional value, following URL parameter conventions. The name
/// cannot be empty, while the value can be <see langword="null"/> (parameter exists without value),
/// empty (parameter with empty value), or contain actual data including comma-separated multiple values.
/// </para>
/// <para>
/// When serialized to a URL string, parameters follow RFC 3986 encoding rules. For example:
/// <list type="bullet">
///   <item>Parameter without value: <c>filter</c></item>
///   <item>Parameter with empty value: <c>filter=</c></item>
///   <item>Parameter with value: <c>filter=active</c></item>
///   <item>Parameter with multiple values: <c>filter=active,archived</c></item>
/// </list>
/// </para>
/// </remarks>
/// <param name="Name">The parameter name, which cannot be empty.</param>
/// <param name="Value">
/// The parameter value. Can be <see langword="null"/> (no value), empty (empty value),
/// or contain data including comma-separated multiple values.
/// </param>
public readonly record struct Parameter(string Name, string? Value)
{
    /// <summary>
    /// Returns a URL-encoded string representation of the parameter.
    /// </summary>
    /// <returns>
    /// The parameter in the format "name=value" with proper URL encoding. If <see cref="Value"/> is
    /// <see langword="null"/>, returns just the encoded name.
    /// </returns>
    /// <remarks>
    /// Both name and value are URL-encoded according to RFC 3986 rules using
    /// <see cref="Uri.EscapeDataString(string)"/>. Special characters are percent-encoded to ensure safe
    /// URL transmission.
    /// </remarks>
    public override string ToString() => Uri.EscapeDataString(this.Name) +
                                       (this.Value is not null
                                           ? '=' + Uri.EscapeDataString(this.Value)
                                           : string.Empty);
}
