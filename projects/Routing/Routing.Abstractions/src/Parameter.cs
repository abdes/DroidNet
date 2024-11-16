// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a URL segment matrix parameter or a URL query parameter, which have a name and an optional value. The name and
/// value are treated as opaque strings without any inherent meaning in this implementation.
/// </summary>
/// <param name="Name">The parameter name.</param>
/// <param name="Value">
/// The parameter value. May contain a comma separated list of values if multiple values were provided for the parameter.
/// </param>
public readonly record struct Parameter(string Name, string? Value)
{
    /// <summary>
    /// Returns a string representation of the parameter.
    /// </summary>
    /// <returns>
    /// The parameter name followed by its value, separated by an equal sign ("="), unless the value is <see langword="null" />,
    /// in which case only the name is returned.
    /// </returns>
    public override string ToString() => Uri.EscapeDataString(this.Name) +
                                         (this.Value is not null
                                             ? '=' + Uri.EscapeDataString(this.Value)
                                             : string.Empty);
}
