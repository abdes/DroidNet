// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a URL segment matrix parameter or a URL query parameter, which
/// have a name and an optional value. The name and the value (when present) are
/// completely opaque strings and do not carry any semantic value inside this
/// implementation.
/// </summary>
/// <param name="Name">The parameter name.</param>
/// <param name="Value">The parameter value.</param>
public readonly record struct Parameter(string Name, string? Value)
{
    /// <summary>Produces a string representation of this parameter.</summary>
    /// <returns>The parameter name followed by its value, separated by the
    /// equal (<see langword="=" />) sign, unless the parameter has no value. In
    /// such case, only the parameter name.</returns>
    public override string ToString() => Uri.EscapeDataString(this.Name) +
                                         (this.Value is not null
                                             ? '=' + Uri.EscapeDataString(this.Value)
                                             : string.Empty);
}
